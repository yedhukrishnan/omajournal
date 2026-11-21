#include "preferences.h"

#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
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

void Preferences::previewFont(const QString &family) {
    const QString trimmed = family.trimmed();
    if (m_previewing && trimmed == m_previewFamily)
        return;
    m_previewFamily = trimmed;
    m_previewing = true;
    emit fontChanged();
}

void Preferences::endPreview() {
    if (!m_previewing)
        return;
    m_previewing = false;
    m_previewFamily.clear();
    emit fontChanged();
}

QString Preferences::resolvedFamily(const QString &family) {
    if (family.isEmpty())
        return QGuiApplication::font().family();
    return QFontInfo(QFont(family)).family();
}

QString Preferences::regularStyle(const QString &family) {
    if (family.isEmpty())
        return {};
    const QString found = QFontInfo(QFont(family)).styleName();
    const bool wrongFace = found.contains("Bold", Qt::CaseInsensitive) || found.contains("Italic", Qt::CaseInsensitive);
    return wrongFace && QFontDatabase::styles(family).contains("Regular") ? QStringLiteral("Regular") : QString();
}

bool Preferences::fontInstalled() const {
    return m_fontFamily.isEmpty() || isGeneric(m_fontFamily)
        || fontFamilies().contains(m_fontFamily, Qt::CaseInsensitive);
}

QStringList Preferences::fontFamilies() const {
    // Read once, when first asked, and kept.
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

QVariantList Preferences::fontChoices() const {
    auto choice = [](const QString &family, const QString &name, const QString &detail = {}) {
        return QVariantMap{{"family", family}, {"name", name}, {"detail", detail}};
    };
    QVariantList choices{choice({}, tr("Default"), resolvedFamily({}))};
    // iA Writer's fonts, made for writing, as Omarchy installs them.
    for (const auto &[family, name] : {std::pair<QString, QString>{"iA Writer Quattro S", "iA Writer Quattro"},
                                       {"iA Writer Duo S", "iA Writer Duo"},
                                       {"iA Writer Mono S", "iA Writer Mono"}})
        if (fontFamilies().contains(family))
            choices << choice(family, name);
    choices << choice("serif", tr("Serif"), resolvedFamily("serif"))
            << choice("monospace", tr("Monospace"), resolvedFamily("monospace"));
    // A font chosen before, or written into settings.json by hand, stays in
    // view, so the list always shows what the editor uses.
    const bool listed = std::any_of(choices.begin(), choices.end(), [this](const QVariant &c) {
        return c.toMap().value("family").toString() == m_fontFamily;
    });
    if (!listed)
        choices << choice(m_fontFamily, m_fontFamily);
    return choices;
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
