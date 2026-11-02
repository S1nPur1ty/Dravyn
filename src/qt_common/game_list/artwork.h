// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCryptographicHash>
#include <QString>

#include "common/common_types.h"
#include "common/fs/path_util.h"

namespace GameListArtwork {

inline QString Path(const QString& game_path, u64 program_id) {
    const auto key = program_id != 0
                         ? QString::number(program_id, 16)
                         : QString::fromLatin1(QCryptographicHash::hash(
                               game_path.toUtf8(), QCryptographicHash::Sha256).toHex());
    const auto directory = Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir) / "artwork";
    return QString::fromStdString(directory.string()) + QLatin1Char('/') + key +
           QStringLiteral(".png");
}

} // namespace GameListArtwork
