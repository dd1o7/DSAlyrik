#pragma once

#include <QString>

// Synthetic lyrics for previews and render tests; original text, not a real song.
inline QString previewLrc()
{
    return QStringLiteral("[00:01.00]Paper boats are sailing through the rain\n"
                          "[00:04.00]Lanterns glow above the sleeping town\n"
                          "[00:07.00]Hold the fire, count the stars again\n"
                          "[00:10.00]Every heart keeps singing to the moon\n");
}
