#include "preferences.h"

#include <QFontDatabase>
#include <algorithm>

#include "settings.h"

namespace {

// Names fontconfig resolves to the desktop's choice for each kind of face;
// they aren't families of their own, so they're always there.
bool isGeneric(const QString &family) {
    return QStringList{"sans-serif", "sans", "serif", "monospace"}.contains(family.toLower());
}

} // namespace

Preferences::Preferences(const QString &settingsFile, QObject *parent)
    : QObject(parent), m_settingsFile(settingsFile.isEmpty() ? Settings::defaultFile() : settingsFile) {
    const QJsonObject settings = Settings::read(m_settingsFile);
    m_fontFamily = settings.value("font").toString().trimmed();
    m_fontSize = std::clamp(settings.value("fontSize").toInt(defaultSize), minimumSize, maximumSize);
}

void Preferences::setFontFamily(const QString &family) {
    const QString trimmed = family.trimmed();
    if (trimmed == m_fontFamily)
        return;
    m_fontFamily = trimmed;
    emit fontChanged();
    save();
}

void Preferences::setFontSize(int size) {
    size = std::clamp(size, minimumSize, maximumSize);
    if (size == m_fontSize)
        return;
    m_fontSize = size;
    emit fontChanged();
    save();
}

bool Preferences::fontInstalled() const {
    return m_fontFamily.isEmpty() || isGeneric(m_fontFamily)
        || fontFamilies().contains(m_fontFamily, Qt::CaseInsensitive);
}

QStringList Preferences::fontFamilies() const {
    // Asked for only once the picker opens, so starting the app doesn't wait
    // on hundreds of fonts.
    if (m_families.isEmpty()) {
        for (const QString &family : QFontDatabase::families())
            if (!QFontDatabase::isPrivateFamily(family) && !family.startsWith('.'))
                m_families << family;
        m_families.removeDuplicates();
        std::sort(m_families.begin(), m_families.end(),
                  [](const QString &a, const QString &b) { return a.localeAwareCompare(b) < 0; });
    }
    return m_families;
}

void Preferences::save() {
    // The default is left out of the file rather than written down, so a
    // later default reaches people who never changed it.
    const QJsonObject changes{
        {"font", m_fontFamily.isEmpty() ? QJsonValue() : QJsonValue(m_fontFamily)},
        {"fontSize", m_fontSize == defaultSize ? QJsonValue() : QJsonValue(m_fontSize)},
    };
    QString error;
    const QString problem = Settings::update(m_settingsFile, changes, &error)
                                ? QString()
                                : tr("Couldn't save the font in %1: %2").arg(m_settingsFile, error);
    if (problem != m_saveError) {
        m_saveError = problem;
        emit saveErrorChanged();
    }
}
