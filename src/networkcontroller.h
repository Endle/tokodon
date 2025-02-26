// SPDX-FileCopyrightText: 2022 Gary Wang <wzc782970009@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later OR GPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <QObject>

class NetworkController : public QObject
{
    Q_OBJECT
public:
    static NetworkController &instance();
    Q_INVOKABLE void setApplicationProxy();

private:
    explicit NetworkController(QObject *parent = nullptr);
};
