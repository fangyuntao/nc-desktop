/*
 * Copyright (C) by Oleksandr Zolotov <alex@nextcloud.com>
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

#include <QFileInfo>
#include <QLoggingCategory>

#include "BasePropagateRemoteDeleteEncrypted.h"
#include "account.h"
#include "clientsideencryptionjobs.h"
#include "deletejob.h"
#include "owncloudpropagator.h"

Q_LOGGING_CATEGORY(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED, "nextcloud.sync.propagator.remove.encrypted")

namespace OCC {

BasePropagateRemoteDeleteEncrypted::BasePropagateRemoteDeleteEncrypted(OwncloudPropagator *propagator, SyncFileItemPtr item, QObject *parent)
    : QObject(parent)
    , _propagator(propagator)
    , _item(item)
{}

QNetworkReply::NetworkError BasePropagateRemoteDeleteEncrypted::networkError() const
{
    return _networkError;
}

QString BasePropagateRemoteDeleteEncrypted::errorString() const
{
    return _errorString;
}

void BasePropagateRemoteDeleteEncrypted::storeFirstError(QNetworkReply::NetworkError err)
{
    if (_networkError == QNetworkReply::NetworkError::NoError) {
        _networkError = err;
    }
}

void BasePropagateRemoteDeleteEncrypted::storeFirstErrorString(const QString &errString)
{
    if (_errorString.isEmpty()) {
        _errorString = errString;
    }
}

void BasePropagateRemoteDeleteEncrypted::fetchMetadataForPath(const QString &path)
{
    qCDebug(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED) << "Folder is encrypted, let's its metadata.";
    _fullFolderRemotePath = _propagator->fullRemotePath(path);

    SyncJournalFileRecord rec;
    if (!_propagator->_journal->getRootE2eFolderRecord(_fullFolderRemotePath, &rec) || !rec.isValid()) {
        taskFailed();
        return;
    }

    _fetchAndUploadE2eeFolderMetadataJob.reset(new FetchAndUploadE2eeFolderMetadataJob(_propagator->account(),
                                                                                       _fullFolderRemotePath,
                                                                                       _propagator->_journal,
                                                                                       rec.path()));

    connect(_fetchAndUploadE2eeFolderMetadataJob.data(),
            &FetchAndUploadE2eeFolderMetadataJob::fetchFinished,
            this,
            &BasePropagateRemoteDeleteEncrypted::slotFetchMetadataJobFinished);
    connect(_fetchAndUploadE2eeFolderMetadataJob.data(),
            &FetchAndUploadE2eeFolderMetadataJob::uploadFinished,
            this,
            &BasePropagateRemoteDeleteEncrypted::slotUpdateMetadataJobFinished);
    _fetchAndUploadE2eeFolderMetadataJob->fetchMetadata();
}

void BasePropagateRemoteDeleteEncrypted::uploadMetadata(bool keepLock)
{
    _fetchAndUploadE2eeFolderMetadataJob->uploadMetadata(keepLock);
}

void BasePropagateRemoteDeleteEncrypted::slotFolderUnLockFinished(const QByteArray &folderId, int statusCode)
{
    if (statusCode != 200) {
        _item->_httpErrorCode = statusCode;
        _errorString = tr("\"%1 Failed to unlock encrypted folder %2\".").arg(statusCode).arg(QString::fromUtf8(folderId));
        _item->_errorString = _errorString;
        taskFailed();
        return;
    }
    qCDebug(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED) << "Folder id" << folderId << "successfully unlocked";
}

void BasePropagateRemoteDeleteEncrypted::slotFetchMetadataJobFinishedRootDeletion(int statusCode, const QString &message)
{
    qCDebug(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED) << "Metadata Received, Preparing it for the new file." << message;

    if (statusCode != 200) {
        qCritical(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED) << "fetch metadata finished with error" << statusCode << message;
        taskFailed();
        return;
    }

    if (!_fetchAndUploadE2eeFolderMetadataJob->folderMetadata() || !_fetchAndUploadE2eeFolderMetadataJob->folderMetadata()->isValid()) {
        qCritical(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED) << "fetch metadata finished with error" << statusCode << message;
        taskFailed();
        return;
    }

    const auto metadatToModify = _fetchAndUploadE2eeFolderMetadataJob->folderMetadata();
    metadatToModify->flagDeletedSet();
    _fetchAndUploadE2eeFolderMetadataJob->setMetadata(metadatToModify);
    _fetchAndUploadE2eeFolderMetadataJob->uploadMetadata(true);
}

void BasePropagateRemoteDeleteEncrypted::slotUpdateMetadataJobFinishedRootDeletion(int statusCode, const QString &message)
{
    if (statusCode != 200) {
        qCDebug(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED) << "Update metadata error for folder" << _fetchAndUploadE2eeFolderMetadataJob->folderId() << "with error" << message;
        qCDebug(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED()) << "Unlocking the folder.";
        taskFailed();
        return;
    }
    Q_ASSERT(!_fullFolderRemotePath.isEmpty());
    runDeleteJob(_fullFolderRemotePath);
}

void BasePropagateRemoteDeleteEncrypted::slotDeleteRemoteItemFinished()
{
    auto *deleteJob = qobject_cast<DeleteJob *>(QObject::sender());

    Q_ASSERT(deleteJob);

    if (!deleteJob) {
        qCCritical(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED) << "Sender is not a DeleteJob instance.";
        taskFailed();
        return;
    }

    const auto err = deleteJob->reply()->error();

    _item->_httpErrorCode = deleteJob->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    _item->_responseTimeStamp = deleteJob->responseTimestamp();
    _item->_requestId = deleteJob->requestId();

    if (err != QNetworkReply::NoError && err != QNetworkReply::ContentNotFoundError) {
        storeFirstErrorString(deleteJob->errorString());
        storeFirstError(err);

        taskFailed();
        return;
    }

    // A 404 reply is also considered a success here: We want to make sure
    // a file is gone from the server. It not being there in the first place
    // is ok. This will happen for files that are in the DB but not on
    // the server or the local file system.
    if (_item->_httpErrorCode != 204 && _item->_httpErrorCode != 404) {
        // Normally we expect "204 No Content"
        // If it is not the case, it might be because of a proxy or gateway intercepting the request, so we must
        // throw an error.
        storeFirstErrorString(tr("Wrong HTTP code returned by server. Expected 204, but received \"%1 %2\".")
                       .arg(_item->_httpErrorCode)
                       .arg(deleteJob->reply()->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString()));

        taskFailed();
        return;
    }

    if (!_propagator->_journal->deleteFileRecord(_item->_originalFile, _item->isDirectory())) {
        qCWarning(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED) << "Failed to delete file record from local DB" << _item->_originalFile;
    }
    _propagator->_journal->commit("Remote Remove");

    unlockFolder(true);
}

void BasePropagateRemoteDeleteEncrypted::deleteRemoteItem(const QString &filename)
{
    qCInfo(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED) << "Deleting nested encrypted item" << filename;
    _fullFolderRemotePath = _propagator->fullRemotePath(filename);
    if (_fetchAndUploadE2eeFolderMetadataJob && _fetchAndUploadE2eeFolderMetadataJob->isFolderLocked()) {
        runDeleteJob(_fullFolderRemotePath);
        return;
    }

    SyncJournalFileRecord rec;
    if (!_propagator->_journal->getRootE2eFolderRecord(_fullFolderRemotePath, &rec) || !rec.isValid()) {
        taskFailed();
        return;
    }

    _fetchAndUploadE2eeFolderMetadataJob.reset(
        new FetchAndUploadE2eeFolderMetadataJob(_propagator->account(), _fullFolderRemotePath, _propagator->_journal, rec.path()));

    connect(_fetchAndUploadE2eeFolderMetadataJob.data(),
            &FetchAndUploadE2eeFolderMetadataJob::fetchFinished,
            this,
            &BasePropagateRemoteDeleteEncrypted::slotFetchMetadataJobFinishedRootDeletion);
    connect(_fetchAndUploadE2eeFolderMetadataJob.data(),
            &FetchAndUploadE2eeFolderMetadataJob::uploadFinished,
            this,
            &BasePropagateRemoteDeleteEncrypted::slotUpdateMetadataJobFinishedRootDeletion);
    _fetchAndUploadE2eeFolderMetadataJob->fetchMetadata();
}

void BasePropagateRemoteDeleteEncrypted::runDeleteJob(const QString &fullRemotePath)
{
    Q_ASSERT(_fetchAndUploadE2eeFolderMetadataJob && _fetchAndUploadE2eeFolderMetadataJob->isFolderLocked());
    if (!_fetchAndUploadE2eeFolderMetadataJob || !_fetchAndUploadE2eeFolderMetadataJob->isFolderLocked()) {
        qCDebug(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED) << "invalid _fetchAndUploadE2eeFolderMetadataJob instance!";
    }
    const auto deleteJob = new DeleteJob(_propagator->account(), fullRemotePath, this);
    deleteJob->setFolderToken(_fetchAndUploadE2eeFolderMetadataJob->folderToken());

    connect(deleteJob, &DeleteJob::finishedSignal, this, &BasePropagateRemoteDeleteEncrypted::slotDeleteRemoteItemFinished);
    deleteJob->start();
}

void BasePropagateRemoteDeleteEncrypted::unlockFolder(bool success)
{
    if (!_fetchAndUploadE2eeFolderMetadataJob->isFolderLocked()) {
        emit finished(true);
        return;
    }

    qCDebug(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED) << "Unlocking folder" << _fetchAndUploadE2eeFolderMetadataJob->folderId();
    
    connect(_fetchAndUploadE2eeFolderMetadataJob.data(), &FetchAndUploadE2eeFolderMetadataJob::folderUnlocked, this, &BasePropagateRemoteDeleteEncrypted::slotFolderUnLockFinished);
    _fetchAndUploadE2eeFolderMetadataJob->unlockFolder(success);
}

void BasePropagateRemoteDeleteEncrypted::taskFailed()
{
    qCDebug(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED) << "Task failed for job" << sender();
    _isTaskFailed = true;
    if (_fetchAndUploadE2eeFolderMetadataJob->isFolderLocked()) {
        unlockFolder(false);
    } else {
        emit finished(false);
    }
}

QSharedPointer<FolderMetadata> BasePropagateRemoteDeleteEncrypted::folderMetadata() const
{
    Q_ASSERT(_fetchAndUploadE2eeFolderMetadataJob->folderMetadata());
    if (!_fetchAndUploadE2eeFolderMetadataJob->folderMetadata()) {
        qCWarning(ABSTRACT_PROPAGATE_REMOVE_ENCRYPTED) << "Metadata is null!";
    }
    return _fetchAndUploadE2eeFolderMetadataJob->folderMetadata();
}

const QByteArray BasePropagateRemoteDeleteEncrypted::folderToken() const
{
    return _fetchAndUploadE2eeFolderMetadataJob->folderToken();
}

} // namespace OCC
