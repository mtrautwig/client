# WebDAV Synchronization Desktop Client

## Introduction

This is a desktop application to synchronizes files against plain WebDAV
servers. It is a fork of the ownCloud Desktop Client, but it does not work
against ownCloud servers. This application has been renamed, so you can use
both clients at the same time.

## Restrictions

You will miss at least the following features from ownCloud, as they are 
not part of the WebDAV standards (even if your service provider may offer 
these in their Browser-based UI):

   * **Resumable uploads**. If uploads are aborted, they will restart from
     the beginning. This will really hurt for large files.
   * **Sharing**. Creating links for sharing files or folders is not
     possible.
   * **Activity and Notifications**. 

## Building the source code

See [Building the Client](http://doc.owncloud.org/desktop/2.3/building.html)
in the ownCloud Desktop Client manual.

## License

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
    for more details.


