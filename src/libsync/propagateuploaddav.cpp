/*
 * Copyright (C) by Olivier Goffart <ogoffart@owncloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "config.h"
#include "propagateupload.h"
#include "owncloudpropagator_p.h"
#include "networkjobs.h"
#include "account.h"
#include "common/syncjournaldb.h"
#include "common/syncjournalfilerecord.h"
#include "common/utility.h"
#include "filesystem.h"
#include "propagatorjobs.h"
#include "common/checksums.h"
#include "syncengine.h"
#include "propagateremotedelete.h"
#include "common/asserts.h"

#include <QNetworkAccessManager>
#include <QFileInfo>
#include <QDir>
#include <cmath>
#include <cstring>

namespace OCC {

void PropagateUploadFileDAV::doStartUpload()
{
    _transferId = qrand() ^ _item->_modtime ^ (_item->_size << 16);

    const SyncJournalDb::UploadInfo progressInfo = propagator()->_journal->getUploadInfo(_item->_file);

    if (progressInfo._valid && progressInfo._modtime == _item->_modtime) {
        _transferId = progressInfo._transferid;
        qCInfo(lcPropagateUpload) << _item->_file << ": Resuming";
    }

    propagator()->reportProgress(*_item, 0);
    startUpload();
}

void PropagateUploadFileDAV::startUpload()
{
    if (propagator()->_abortRequested.fetchAndAddRelaxed(0))
        return;

    quint64 fileSize = _item->_size;
    auto headers = PropagateUploadFileCommon::headers();

    QString path = _item->_file;

    UploadDevice *device = new UploadDevice(&propagator()->_bandwidthManager);
    qCDebug(lcPropagateUpload) << fileSize;

    if (!_transmissionChecksumHeader.isEmpty()) {
        qCInfo(lcPropagateUpload) << propagator()->_remoteFolder + path << _transmissionChecksumHeader;
        headers[checkSumHeaderC] = _transmissionChecksumHeader;
    }

    const QString fileName = propagator()->getFilePath(_item->_file);
    if (!device->prepareAndOpen(fileName, 0, fileSize)) {
        qCWarning(lcPropagateUpload) << "Could not prepare upload device: " << device->errorString();

        // If the file is currently locked, we want to retry the sync
        // when it becomes available again.
        if (FileSystem::isFileLocked(fileName)) {
            emit propagator()->seenLockedFile(fileName);
        }
        // Soft error because this is likely caused by the user modifying his files while syncing
        abortWithError(SyncFileItem::SoftError, device->errorString());
        delete device;
        return;
    }

    // job takes ownership of device via a QScopedPointer. Job deletes itself when finishing
    PUTFileJob *job = new PUTFileJob(propagator()->account(), propagator()->_remoteFolder + path, device, headers, 0, this);
    _jobs.append(job);
    connect(job, &PUTFileJob::finishedSignal, this, &PropagateUploadFileDAV::slotPutFinished);
    connect(job, &PUTFileJob::uploadProgress, this, &PropagateUploadFileDAV::slotUploadProgress);
    connect(job, &PUTFileJob::uploadProgress, device, &UploadDevice::slotJobUploadProgress);
    connect(job, &QObject::destroyed, this, &PropagateUploadFileCommon::slotJobDestroyed);
    job->start();
    propagator()->_activeJobList.append(this);
    propagator()->scheduleNextJob();
}

void PropagateUploadFileDAV::slotPutFinished()
{
    PUTFileJob *job = qobject_cast<PUTFileJob *>(sender());
    ASSERT(job);

    slotJobDestroyed(job); // remove it from the _jobs list

    propagator()->_activeJobList.removeOne(this);

    if (_finished) {
        // We have sent the finished signal already. We don't need to handle any remaining jobs
        return;
    }

    QNetworkReply::NetworkError err = job->reply()->error();

    if (err != QNetworkReply::NoError) {
        _item->_httpErrorCode = job->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (checkForProblemsWithShared(_item->_httpErrorCode,
                tr("The file was edited locally but is part of a read only share. "
                   "It is restored and your edit is in the conflict file."))) {
            return;
        }
        commonErrorHandling(job);
        return;
    }

    _item->_httpErrorCode = job->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // The server needs some time to process the request and provide us with a poll URL
    if (_item->_httpErrorCode == 202) {
        _finished = true;
        QString path = QString::fromUtf8(job->reply()->rawHeader("OC-Finish-Poll"));
        if (path.isEmpty()) {
            done(SyncFileItem::NormalError, tr("Poll URL missing"));
            return;
        }
        startPollJob(path);
        return;
    }

    // Check the file again post upload.
    // Two cases must be considered separately: If the upload is finished,
    // the file is on the server and has a changed ETag. In that case,
    // the etag has to be properly updated in the client journal, and because
    // of that we can bail out here with an error. But we can reschedule a
    // sync ASAP.
    QByteArray etag = getEtagFromReply(job->reply());
    bool finished = etag.length() > 0;

    // Check if the file still exists
    const QString fullFilePath(propagator()->getFilePath(_item->_file));
    if (!FileSystem::fileExists(fullFilePath)) {
        if (!finished) {
            abortWithError(SyncFileItem::SoftError, tr("The local file was removed during sync."));
            return;
        } else {
            propagator()->_anotherSyncNeeded = true;
        }
    }

    // Check whether the file changed since discovery.
    if (!FileSystem::verifyFileUnchanged(fullFilePath, _item->_size, _item->_modtime)) {
        propagator()->_anotherSyncNeeded = true;
        if (!finished) {
            abortWithError(SyncFileItem::SoftError, tr("Local file changed during sync."));
            return;
        }
    }

    if (!finished) {
        // Deletes an existing blacklist entry on successful upload
        if (_item->_hasBlacklistEntry) {
            propagator()->_journal->wipeErrorBlacklistEntry(_item->_file);
            _item->_hasBlacklistEntry = false;
        }

        SyncJournalDb::UploadInfo pi;
        pi._valid = true;
        pi._chunk = 0; // next chunk to start with
        pi._transferid = _transferId;
        pi._modtime = _item->_modtime;
        pi._errorCount = 0; // successful chunk upload resets
        propagator()->_journal->setUploadInfo(_item->_file, pi);
        propagator()->_journal->commit("Upload info");
        startUpload();
        return;
    }

    // the following code only happens after all upload completed.
    _finished = true;
    // the file id should only be empty for new files up- or downloaded
    QByteArray fid = job->reply()->rawHeader("OC-FileID");
    if (!fid.isEmpty()) {
        if (!_item->_fileId.isEmpty() && _item->_fileId != fid) {
            qCWarning(lcPropagateUpload) << "File ID changed!" << _item->_fileId << fid;
        }
        _item->_fileId = fid;
    }

    _item->_etag = etag;

    _item->_responseTimeStamp = job->responseTimestamp();

    if (job->reply()->rawHeader("X-OC-MTime") != "accepted") {
        // X-OC-MTime is supported since owncloud 5.0.   But not when chunking.
        // Normally Owncloud 6 always puts X-OC-MTime
        qCWarning(lcPropagateUpload) << "Server does not support X-OC-MTime" << job->reply()->rawHeader("X-OC-MTime");
        // Well, the mtime was not set
        // FIXME why is this important?!?
        //done(SyncFileItem::SoftError, "Server does not support X-OC-MTime");
    }

#ifdef WITH_TESTING
    // performance logging
    quint64 duration = _stopWatch.stop();
    qCDebug(lcPropagateUpload) << "*==* duration UPLOAD" << _item->_size
                               << _stopWatch.durationOfLap(QLatin1String("ContentChecksum"))
                               << _stopWatch.durationOfLap(QLatin1String("TransmissionChecksum"))
                               << duration;
    // The job might stay alive for the whole sync, release this tiny bit of memory.
    _stopWatch.reset();
#endif

    finalize();
}


void PropagateUploadFileDAV::slotUploadProgress(qint64 sent, qint64 total)
{
    // Completion is signaled with sent=0, total=0; avoid accidentally
    // resetting progress due to the sent being zero by ignoring it.
    // finishedSignal() is bound to be emitted soon anyway.
    // See https://bugreports.qt.io/browse/QTBUG-44782.
    if (sent == 0 && total == 0) {
        return;
    }

    propagator()->reportProgress(*_item, sent);
}
}
