/*
 * Copyright © 2023, Matthieu Gallien <matthieu.gallien@nextcloud.com>
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

#ifndef CLIENTSIDETOKENSELECTOR_H
#define CLIENTSIDETOKENSELECTOR_H

#include "accountfwd.h"

#include <QObject>

namespace OCC
{

class ClientSideTokenSelector : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isSetup READ isSetup NOTIFY isSetupChanged)

    Q_PROPERTY(QVariantList discoveredCertificates READ discoveredCertificates NOTIFY discoveredCertificatesChanged)

    Q_PROPERTY(QString serialNumber READ serialNumber WRITE setSerialNumber NOTIFY serialNumberChanged)

public:
    explicit ClientSideTokenSelector(QObject *parent = nullptr);

    [[nodiscard]] bool isSetup() const;

    [[nodiscard]] QVariantList discoveredCertificates() const;

    [[nodiscard]] QString serialNumber() const;

public slots:

    void searchForCertificates(const OCC::AccountPtr &account);

    void setSerialNumber(const QString &serialNumber);

signals:

    void isSetupChanged();

    void discoveredCertificatesChanged();

    void certificateIndexChanged();

    void serialNumberChanged();

    void failedToInitialize(const OCC::AccountPtr &account);

private:

    QVariantList _discoveredCertificates;

    QString _serialNumber;
};

}

#endif // CLIENTSIDETOKENSELECTOR_H
