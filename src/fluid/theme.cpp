/****************************************************************************
 * This file is part of Fluid.
 *
 * Copyright (c) 2012 Pier Luigi Fiorini
 * Copyright (c) 2006-2007 Aaron Seigo
 *
 * Author(s):
 *    Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>
 *    Aaron Seigo <aseigo@kde.org>
 *
 * $BEGIN_LICENSE:LGPL-ONLY$
 *
 * This file may be used under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation and
 * appearing in the file LICENSE.LGPL included in the packaging of
 * this file, either version 2.1 of the License, or (at your option) any
 * later version.  Please review the following information to ensure the
 * GNU Lesser General Public License version 2.1 requirements
 * will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 *
 * If you have questions regarding the use of this file, please contact
 * us via http://www.maui-project.org/.
 *
 * $END_LICENSE$
 ***************************************************************************/

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMutableListIterator>
#include <QPair>
#include <QPixmapCache>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStringBuilder>
#include <QTimer>
#include <QUrl>

#include <VDesktopFile>
#include <VSettings>

#include "theme.h"

namespace Fluid
{
    enum styles {
        DEFAULTSTYLE,
        SVGSTYLE
    };

    enum CacheType {
        NoCache = 0,
        PixmapCache = 1,
        SvgElementsCache = 2
    };
    Q_DECLARE_FLAGS(CacheTypes, CacheType)
    Q_DECLARE_OPERATORS_FOR_FLAGS(CacheTypes)

    class ThemePrivate
    {
    public:
        ThemePrivate(Theme *theme)
            : q(theme),
#if 0
              colorScheme(QPalette::Active, KColorScheme::Window, KSharedConfigPtr(0)),
              buttonColorScheme(QPalette::Active, KColorScheme::Button, KSharedConfigPtr(0)),
              viewColorScheme(QPalette::Active, KColorScheme::View, KSharedConfigPtr(0)),
#endif
              cachesToDiscard(NoCache),
              locolor(false),
              compositingActive(true),
              blurActive(false),
              isDefault(false),
              useGlobal(true),
              useNativeWidgetStyle(false) {
            generalFont = QApplication::font();

            saveTimer = new QTimer(q);
            saveTimer->setSingleShot(true);
            saveTimer->setInterval(600);
            QObject::connect(saveTimer, SIGNAL(timeout()), q, SLOT(scheduledCacheUpdate()));

            updateNotificationTimer = new QTimer(q);
            updateNotificationTimer->setSingleShot(true);
            updateNotificationTimer->setInterval(500);
            QObject::connect(updateNotificationTimer, SIGNAL(timeout()), q, SLOT(notifyOfChanged()));

#if 0
            if (QPixmap::defaultDepth() > 8) {
                QObject::connect(KWindowSystem::self(), SIGNAL(compositingChanged(bool)), q, SLOT(compositingChanged(bool)));
#if HAVE_X11
                //watch for blur effect property changes as well
                if (!s_blurEffectWatcher) {
                    s_blurEffectWatcher = new EffectWatcher("_KDE_NET_WM_BLUR_BEHIND_REGION");
                }

                QObject::connect(s_blurEffectWatcher, SIGNAL(effectChanged(bool)), q, SLOT(blurBehindChanged(bool)));
#endif
            }
#endif
        }

        QString findInTheme(const QString &image, const QString &theme, bool cache = true);
        void compositingChanged(bool active);
        void discardCache(CacheTypes caches);
        void scheduledCacheUpdate();
        void scheduleThemeChangeNotification(CacheTypes caches);
        void notifyOfChanged();
        void colorsChanged();
        void blurBehindChanged(bool blur);
        void settingsChanged(const QString &key, const QVariant &value);
        void setThemeName(const QString &themeName, bool writeSettings);
        void onAppExitCleanup();

        const QString processStyleSheet(const QString &css);

        static const char *defaultTheme;
        static const char *systemColorsTheme;
#if HAVE_X11
        static EffectWatcher *s_blurEffectWatcher;
#endif

        Theme *q;
        QString themeName;
        QList<QString> fallbackThemes;
#if 0
        KSharedConfigPtr colors;
        KColorScheme colorScheme;
        KColorScheme buttonColorScheme;
        KColorScheme viewColorScheme;
#endif
        VSettings *settings;
        QFont generalFont;
        QString pixmapCachePrefix;
#if 0
        KSharedConfigPtr svgElementsCache;
#endif
        QHash<QString, QSet<QString> > invalidElements;
        QHash<QString, QPixmap> pixmapsToCache;
        QHash<QString, QString> keysToCache;
        QHash<QString, QString> idsToCache;
        QHash<styles, QString> cachedStyleSheets;
        QHash<QString, QString> discoveries;
        QTimer *saveTimer;
        QTimer *updateNotificationTimer;
        int toolTipDelay;
        CacheTypes cachesToDiscard;

        bool locolor : 1;
        bool compositingActive : 1;
        bool blurActive : 1;
        bool isDefault : 1;
        bool useGlobal : 1;
        bool useNativeWidgetStyle : 1;
    };

    const char *ThemePrivate::defaultTheme = "Vanish";

    // the system colors theme is used to cache unthemed svgs with colorization needs
    // these svgs do not follow the theme's colors, but rather the system colors
    const char *ThemePrivate::systemColorsTheme = "internal-system-colors";
#if HAVE_X11
    EffectWatcher *ThemePrivate::s_blurEffectWatcher = 0;
#endif

    void ThemePrivate::onAppExitCleanup()
    {
        pixmapsToCache.clear();
    }

    QString ThemePrivate::findInTheme(const QString &image, const QString &theme, bool cache)
    {
        if (cache && discoveries.contains(image)) {
            return discoveries[image];
        }

        QString search;

        if (locolor) {
            search = QLatin1Literal("themes/") % theme % QLatin1Literal("/locolor/") % image;
            search =  QStandardPaths::locate(QStandardPaths::GenericDataLocation, search);
        } else if (!compositingActive) {
            search = QLatin1Literal("themes/") % theme % QLatin1Literal("/opaque/") % image;
            search =  QStandardPaths::locate(QStandardPaths::GenericDataLocation, search);
        } else {
            search = QLatin1Literal("themes/") % theme % QLatin1Literal("/translucent/") % image;
            search =  QStandardPaths::locate(QStandardPaths::GenericDataLocation, search);
        }

        //not found or compositing enabled
        if (search.isEmpty()) {
            search = QLatin1Literal("themes/") % theme % QLatin1Char('/') % image;
            search =  QStandardPaths::locate(QStandardPaths::GenericDataLocation, search);
        }

        if (cache && !search.isEmpty()) {
            discoveries.insert(image, search);
        }

        return search;
    }

    void ThemePrivate::compositingChanged(bool active)
    {
#if HAVE_X11
        if (compositingActive != active) {
            compositingActive = active;
            //qDebug() << QTime::currentTime();
            scheduleThemeChangeNotification(PixmapCache | SvgElementsCache);
        }
#endif
    }

    void ThemePrivate::discardCache(CacheTypes caches)
    {
        if (caches & PixmapCache) {
            pixmapsToCache.clear();
            saveTimer->stop();
            QPixmapCache::clear();
        }

        cachedStyleSheets.clear();

        if (caches & SvgElementsCache) {
            discoveries.clear();
            invalidElements.clear();

#if 0
            if (svgElementsCache) {
                QFile f(svgElementsCache->name());
                svgElementsCache = 0;
                f.remove();
            }

            const QString svgElementsFile = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QLatin1Char('/') + "plasma-svgelements-" + themeName;
            svgElementsCache = KSharedConfig::openConfig(svgElementsFile);
#endif
        }
    }

    void ThemePrivate::scheduledCacheUpdate()
    {
        QHashIterator<QString, QPixmap> it(pixmapsToCache);
        while (it.hasNext()) {
            it.next();
            QPixmapCache::insert(pixmapCachePrefix + "_" + idsToCache[it.key()], it.value());
        }

        pixmapsToCache.clear();
        keysToCache.clear();
        idsToCache.clear();
    }

    void ThemePrivate::colorsChanged()
    {
#if 0
        colorScheme = KColorScheme(QPalette::Active, KColorScheme::Window, colors);
        buttonColorScheme = KColorScheme(QPalette::Active, KColorScheme::Button, colors);
        viewColorScheme = KColorScheme(QPalette::Active, KColorScheme::View, colors);
        scheduleThemeChangeNotification(PixmapCache);
#endif
    }

    void ThemePrivate::blurBehindChanged(bool blur)
    {
        if (blurActive != blur) {
            blurActive = blur;
            scheduleThemeChangeNotification(PixmapCache | SvgElementsCache);
        }
    }

    void ThemePrivate::scheduleThemeChangeNotification(CacheTypes caches)
    {
        cachesToDiscard |= caches;
        updateNotificationTimer->start();
    }

    void ThemePrivate::notifyOfChanged()
    {
        //qDebug() << cachesToDiscard;
        discardCache(cachesToDiscard);
        cachesToDiscard = NoCache;
        emit q->themeChanged();
    }

    const QString ThemePrivate::processStyleSheet(const QString &css)
    {
        QString stylesheet;
        if (css.isEmpty()) {
            stylesheet = cachedStyleSheets.value(DEFAULTSTYLE);
            if (stylesheet.isEmpty()) {
                stylesheet = QString("\n\
                        body {\n\
                            color: %textcolor;\n\
                            font-size: %fontsize;\n\
                            font-family: %fontfamily;\n\
                        }\n\
                        a:active  { color: %activatedlink; }\n\
                        a:link    { color: %link; }\n\
                        a:visited { color: %visitedlink; }\n\
                        a:hover   { color: %hoveredlink; text-decoration: none; }\n\
                        ");
                stylesheet = processStyleSheet(stylesheet);
                cachedStyleSheets.insert(DEFAULTSTYLE, stylesheet);
            }

            return stylesheet;
        } else if (css == "SVG") {
            stylesheet = cachedStyleSheets.value(SVGSTYLE);
            if (stylesheet.isEmpty()) {
                QString skel = ".ColorScheme-%1{color:%2;}";

                stylesheet += skel.arg("Text", "%textcolor");
                stylesheet += skel.arg("Background", "%backgroundcolor");

                stylesheet += skel.arg("ButtonText", "%buttontextcolor");
                stylesheet += skel.arg("ButtonBackground", "%buttonbackgroundcolor");
                stylesheet += skel.arg("ButtonHover", "%buttonhovercolor");
                stylesheet += skel.arg("ButtonFocus", "%buttonfocuscolor");

                stylesheet += skel.arg("ViewText", "%viewtextcolor");
                stylesheet += skel.arg("ViewBackground", "%viewbackgroundcolor");
                stylesheet += skel.arg("ViewHover", "%viewhovercolor");
                stylesheet += skel.arg("ViewFocus", "%viewfocuscolor");

                stylesheet = processStyleSheet(stylesheet);
                cachedStyleSheets.insert(SVGSTYLE, stylesheet);
            }

            return stylesheet;
        } else {
            stylesheet = css;
        }

#if 0
        QHash<QString, QString> elements;
        // If you add elements here, make sure their names are sufficiently unique to not cause
        // clashes between element keys
        elements["%textcolor"] = q->color(Theme::TextColor).name();
        elements["%backgroundcolor"] = q->color(Theme::BackgroundColor).name();
        elements["%visitedlink"] = q->color(Theme::VisitedLinkColor).name();
        elements["%activatedlink"] = q->color(Theme::HighlightColor).name();
        elements["%hoveredlink"] = q->color(Theme::HighlightColor).name();
        elements["%link"] = q->color(Theme::LinkColor).name();
        elements["%buttontextcolor"] = q->color(Theme::ButtonTextColor).name();
        elements["%buttonbackgroundcolor"] = q->color(Theme::ButtonBackgroundColor).name();
        elements["%buttonhovercolor"] = q->color(Theme::ButtonHoverColor).name();
        elements["%buttonfocuscolor"] = q->color(Theme::ButtonFocusColor).name();
        elements["%viewtextcolor"] = q->color(Theme::ViewTextColor).name();
        elements["%viewbackgroundcolor"] = q->color(Theme::ViewBackgroundColor).name();
        elements["%viewhovercolor"] = q->color(Theme::ViewHoverColor).name();
        elements["%viewfocuscolor"] = q->color(Theme::ViewFocusColor).name();

        QFont font = q->font(Theme::DefaultFont);
        elements["%fontsize"] = QString("%1pt").arg(font.pointSize());
        elements["%fontfamily"] = font.family().split('[').first();
        elements["%smallfontsize"] = QString("%1pt").arg(KGlobalSettings::smallestReadableFont().pointSize());

        QHash<QString, QString>::const_iterator it = elements.constBegin();
        QHash<QString, QString>::const_iterator itEnd = elements.constEnd();
        for (; it != itEnd; ++it)
            stylesheet.replace(it.key(), it.value());
#endif
        return stylesheet;
    }

    class ThemeSingleton
    {
    public:
        ThemeSingleton() {
            self.d->isDefault = true;

#if 0
            QObject::connect(self.d->settings, SIGNAL(changed(QString, QVariant)),
                             &self, SLOT(settingsChanged(QString, QVariant)));
#endif
        }

        Theme self;
    };

    Q_GLOBAL_STATIC(ThemeSingleton, privateThemeSelf)

    Theme *Theme::defaultTheme()
    {
        return &privateThemeSelf()->self;
    }

    Theme::Theme(QObject *parent)
        : QObject(parent),
          d(new ThemePrivate(this))
    {
        // Default theme settings
        d->setThemeName(QStringLiteral("Vanish"), false);
        d->toolTipDelay = 700;

        if (QCoreApplication::instance()) {
            connect(QCoreApplication::instance(), SIGNAL(aboutToQuit()),
                    this, SLOT(onAppExitCleanup()));
        }
    }

    Theme::Theme(const QString &themeName, QObject *parent)
        : QObject(parent),
          d(new ThemePrivate(this))
    {
        setThemeName(themeName);
        if (QCoreApplication::instance())
            connect(QCoreApplication::instance(), SIGNAL(aboutToQuit()),
                    this, SLOT(onAppExitCleanup()));
    }

    Theme::~Theme()
    {
#if 0
        if (d->svgElementsCache) {
            QHashIterator<QString, QSet<QString> > it(d->invalidElements);
            while (it.hasNext()) {
                it.next();
                KConfigGroup imageGroup(d->svgElementsCache, it.key());
                imageGroup.writeEntry("invalidElements", it.value().toList()); //FIXME: add QSet support to KConfig
            }
        }
#endif

        d->onAppExitCleanup();
        delete d;
    }

    QStringList Theme::listThemeInfo()
    {
        const QStringList themes = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                                             "themes/*/metadata.desktop");
        return themes;
    }

    void ThemePrivate::settingsChanged(const QString &key, const QVariant &value)
    {
        if (key == "theme") {
            // TODO: take tooltipdelay from qplatformtheme
            setThemeName(value.toString(), false);
            toolTipDelay = 700;
        }
    }

    void Theme::setThemeName(const QString &themeName)
    {
        d->setThemeName(themeName, true);
    }

    void ThemePrivate::setThemeName(const QString &tempThemeName, bool writeSettings)
    {
        //qDebug() << tempThemeName;
        QString theme = tempThemeName;
        if (theme.isEmpty() || theme == themeName) {
            // let's try and get the default theme at least
            if (themeName.isEmpty()) {
                theme = ThemePrivate::defaultTheme;
            } else {
                return;
            }
        }

        // we have one special theme: essentially a dummy theme used to cache things with
        // the system colors.
        bool realTheme = theme != systemColorsTheme;
        if (realTheme) {
            QString themePath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QLatin1Literal("themes/") % theme % QLatin1Char('/'));
            if (themePath.isEmpty() && themeName.isEmpty()) {
                themePath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "themes/default", QStandardPaths::LocateDirectory);

                if (themePath.isEmpty()) {
                    return;
                }

                theme = ThemePrivate::defaultTheme;
            }
        }

        // check again as ThemePrivate::defaultTheme might be empty
        if (themeName == theme) {
            return;
        }

        themeName = theme;

        // load the color scheme config
        const QString colorsFile = realTheme ? QStandardPaths::locate(QStandardPaths::GenericDataLocation, QLatin1Literal("themes/") % theme % QLatin1Literal("/colors"))
                                   : QString();

        //qDebug() << "we're going for..." << colorsFile << "*******************";

        // load the wallpaper settings, if any
        if (realTheme) {
            const QString metadataPath(QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                              QLatin1Literal("themes/") % theme % QLatin1Literal("/metadata.desktop")));

            QSettings metadata(metadataPath, QSettings::IniFormat);

            metadata.beginGroup("Settings");
            useNativeWidgetStyle = metadata.value("UseNativeWidgetStyle", false).toBool();
            QString fallback = metadata.value("FallbackTheme", QString()).toString();

            fallbackThemes.clear();
            while (!fallback.isEmpty() && !fallbackThemes.contains(fallback)) {
                fallbackThemes.append(fallback);

                QString metadataPath(QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                            QLatin1Literal("themes/") % theme % QLatin1Literal("/metadata.desktop")));
                QSettings metadata(metadataPath, QSettings::IniFormat);
                metadata.beginGroup("Settings");
                fallback = metadata.value("FallbackTheme", QString()).toString();
                metadata.endGroup();
            }

            if (!fallbackThemes.contains("oxygen"))
                fallbackThemes.append("oxygen");

            if (!fallbackThemes.contains(ThemePrivate::defaultTheme))
                fallbackThemes.append(ThemePrivate::defaultTheme);
        }

#if 0
        if (colorsFile.isEmpty()) {
            colors = 0;
            QObject::connect(KGlobalSettings::self(), SIGNAL(kdisplayPaletteChanged()),
                             q, SLOT(colorsChanged()), Qt::UniqueConnection);
        } else {
            QObject::disconnect(KGlobalSettings::self(), SIGNAL(kdisplayPaletteChanged()),
                                q, SLOT(colorsChanged()));
            colors = KSharedConfig::openConfig(colorsFile);
        }

        colorScheme = KColorScheme(QPalette::Active, KColorScheme::Window, colors);
        buttonColorScheme = KColorScheme(QPalette::Active, KColorScheme::Button, colors);
        viewColorScheme = KColorScheme(QPalette::Active, KColorScheme::View, colors);

        if (realTheme && isDefault && writeSettings) {
            // we're the default theme, let's save our state
            KConfigGroup &cg = config();
            if (ThemePrivate::defaultTheme == themeName) {
                cg.deleteEntry("name");
            } else {
                cg.writeEntry("name", themeName);
            }
            cg.sync();
        }
#endif

        // Pixmap cache prefix
        pixmapCachePrefix = "fluid_theme_" + themeName;

        scheduleThemeChangeNotification(SvgElementsCache);
    }

    QString Theme::themeName() const
    {
        return d->themeName;
    }

    QString Theme::imagePath(const QString &name) const
    {
        // look for a compressed svg file in the theme
        if (name.contains("../") || name.isEmpty()) {
            // we don't support relative paths
            //qDebug() << "Theme says: bad image path " << name;
            return QString();
        }

        const QString svgzName = name % QLatin1Literal(".svgz");
        QString path = d->findInTheme(svgzName, d->themeName);

        if (path.isEmpty()) {
            // try for an uncompressed svg file
            const QString svgName = name % QLatin1Literal(".svg");
            path = d->findInTheme(svgName, d->themeName);

            // search in fallback themes if necessary
            for (int i = 0; path.isEmpty() && i < d->fallbackThemes.count(); ++i) {
                if (d->themeName == d->fallbackThemes[i]) {
                    continue;
                }

                // try a compressed svg file in the fallback theme
                path = d->findInTheme(svgzName, d->fallbackThemes[i]);

                if (path.isEmpty()) {
                    // try an uncompressed svg file in the fallback theme
                    path = d->findInTheme(svgName, d->fallbackThemes[i]);
                }
            }
        }

        /*
        if (path.isEmpty()) {
        #ifdef DEBUG
            qDebug() << "Theme says: bad image path " << name;
        #endif
        }
        */

        return path;
    }

    QString Theme::styleSheet(const QString &css) const
    {
        return d->processStyleSheet(css);
    }

    bool Theme::currentThemeHasImage(const QString &name) const
    {
        if (name.contains("../")) {
            // we don't support relative paths
            return false;
        }

        return !(d->findInTheme(name % QLatin1Literal(".svgz"), d->themeName, false).isEmpty()) ||
               !(d->findInTheme(name % QLatin1Literal(".svg"), d->themeName, false).isEmpty());
    }

#if 0
    KSharedConfigPtr Theme::colorScheme() const
    {
        return d->colors;
    }

    QColor Theme::color(ColorRole role) const
    {
        switch (role) {
            case TextColor:
                return d->colorScheme.foreground(KColorScheme::NormalText).color();

            case HighlightColor:
                return d->colorScheme.decoration(KColorScheme::HoverColor).color();

            case BackgroundColor:
                return d->colorScheme.background(KColorScheme::NormalBackground).color();

            case ButtonTextColor:
                return d->buttonColorScheme.foreground(KColorScheme::NormalText).color();

            case ButtonBackgroundColor:
                return d->buttonColorScheme.background(KColorScheme::NormalBackground).color();

            case ButtonHoverColor:
                return d->buttonColorScheme.decoration(KColorScheme::HoverColor).color();

            case ButtonFocusColor:
                return d->buttonColorScheme.decoration(KColorScheme::FocusColor).color();

            case ViewTextColor:
                return d->viewColorScheme.foreground(KColorScheme::NormalText).color();

            case ViewBackgroundColor:
                return d->viewColorScheme.background(KColorScheme::NormalBackground).color();

            case ViewHoverColor:
                return d->viewColorScheme.decoration(KColorScheme::HoverColor).color();

            case ViewFocusColor:
                return d->viewColorScheme.decoration(KColorScheme::FocusColor).color();

            case LinkColor:
                return d->viewColorScheme.foreground(KColorScheme::LinkText).color();

            case VisitedLinkColor:
                return d->viewColorScheme.foreground(KColorScheme::VisitedText).color();
        }

        return QColor();
    }
#endif

    void Theme::setFont(const QFont &font, FontRole role)
    {
        Q_UNUSED(role)
        d->generalFont = font;
    }

    QFont Theme::font(FontRole role) const
    {
#if 0 // TODO: platformtheme
        switch (role) {
            case DesktopFont: {
                KConfigGroup cg(KSharedConfig::openConfig(), "General");
                return cg.readEntry("desktopFont", d->generalFont);
            }
            break;

            case DefaultFont:
            default:
                return d->generalFont;
                break;

            case SmallestFont:
                return KGlobalSettings::smallestReadableFont();
                break;
        }
#endif

        return d->generalFont;
    }

    QFontMetrics Theme::fontMetrics() const
    {
        //TODO: allow this to be overridden with a plasma specific font?
        return QFontMetrics(d->generalFont);
    }

    bool Theme::windowTranslucencyEnabled() const
    {
        return d->compositingActive;
    }

    void Theme::setUseGlobalSettings(bool useGlobal)
    {
        if (d->useGlobal == useGlobal) {
            return;
        }

        d->useGlobal = useGlobal;
        d->settings = new VSettings("org.hawaii.desktop.interface");
        d->themeName.clear();
    }

    bool Theme::useGlobalSettings() const
    {
        return d->useGlobal;
    }

    bool Theme::useNativeWidgetStyle() const
    {
        return d->useNativeWidgetStyle;
    }

    bool Theme::findInCache(const QString &key, QPixmap &pix, unsigned int lastModified)
    {
#if 0
        if (lastModified != 0 && lastModified > uint(d->pixmapCache->lastModifiedTime()))
            return false;

        const QString id = d->keysToCache.value(key);
        if (d->pixmapsToCache.contains(id)) {
            pix = d->pixmapsToCache.value(id);
            return !pix.isNull();
        }

        QPixmap temp;
        if (QPixmapCache::find(pixmapCachePrefix + "_" + key, &temp) && !temp.isNull()) {
            pix = temp;
            return true;
        }
#endif

        return false;
    }

    void Theme::insertIntoCache(const QString &key, const QPixmap &pix)
    {
        QPixmapCache::insert(d->pixmapCachePrefix + "_" + key, pix);
    }

    void Theme::insertIntoCache(const QString &key, const QPixmap &pix, const QString &id)
    {
        d->pixmapsToCache.insert(id, pix);

        if (d->idsToCache.contains(id))
            d->keysToCache.remove(d->idsToCache[id]);

        d->keysToCache.insert(key, id);
        d->idsToCache.insert(id, key);
        d->saveTimer->start();
    }

    bool Theme::findInRectsCache(const QString &image, const QString &element, QRectF &rect) const
    {
#if 0
        if (!d->svgElementsCache)
            return false;

        KConfigGroup imageGroup(d->svgElementsCache, image);
        rect = imageGroup.readEntry(element % QLatin1Literal("Size"), QRectF());

        if (rect.isValid()) {
            return true;
        }

        //Name starting by _ means the element is empty and we're asked for the size of
        //the whole image, so the whole image is never invalid
        if (element.indexOf('_') <= 0) {
            return false;
        }

        bool invalid = false;

        QHash<QString, QSet<QString> >::iterator it = d->invalidElements.find(image);
        if (it == d->invalidElements.end()) {
            QSet<QString> elements = imageGroup.readEntry("invalidElements", QStringList()).toSet();
            d->invalidElements.insert(image, elements);
            invalid = elements.contains(element);
        } else {
            invalid = it.value().contains(element);
        }

        return invalid;
#else
        return false;
#endif
    }

    QStringList Theme::listCachedRectKeys(const QString &image) const
    {
#if 0
        if (!d->svgElementsCache)
            return QStringList();

        KConfigGroup imageGroup(d->svgElementsCache, image);
        QStringList keys = imageGroup.keyList();

        QMutableListIterator<QString> i(keys);
        while (i.hasNext()) {
            QString key = i.next();
            if (key.endsWith("Size")) {
                // The actual cache id used from outside doesn't end on "Size".
                key.resize(key.size() - 4);
                i.setValue(key);
            } else {
                i.remove();
            }
        }
        return keys;
#else
        return QStringList();
#endif
    }

    void Theme::insertIntoRectsCache(const QString &image, const QString &element, const QRectF &rect)
    {
#if 0
        if (!d->svgElementsCache)
            return;

        if (rect.isValid()) {
            KConfigGroup imageGroup(d->svgElementsCache, image);
            imageGroup.writeEntry(element % QLatin1Literal("Size"), rect);
        } else {
            QHash<QString, QSet<QString> >::iterator it = d->invalidElements.find(image);
            if (it == d->invalidElements.end()) {
                d->invalidElements[image].insert(element);
            } else if (!it.value().contains(element)) {
                if (it.value().count() > 1000) {
                    it.value().erase(it.value().begin());
                }

                it.value().insert(element);
            }
        }
#endif
    }

    void Theme::invalidateRectsCache(const QString &image)
    {
#if 0
        if (d->svgElementsCache) {
            KConfigGroup imageGroup(d->svgElementsCache, image);
            imageGroup.deleteGroup();
        }

        d->invalidElements.remove(image);
#endif
    }

    void Theme::releaseRectsCache(const QString &image)
    {
#if 0
        QHash<QString, QSet<QString> >::iterator it = d->invalidElements.find(image);
        if (it != d->invalidElements.end()) {
            if (!d->svgElementsCache) {
                KConfigGroup imageGroup(d->svgElementsCache, it.key());
                imageGroup.writeEntry("invalidElements", it.value().toList());
            }

            d->invalidElements.erase(it);
        }
#endif
    }

    void Theme::setCacheLimit(int kbytes)
    {
        Q_UNUSED(kbytes)
        // Too late for you bub.
        // QPixmapCache::setCacheLimit(kbytes);
    }

    QUrl Theme::homepage() const
    {
        const QString metadataPath(QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                          QLatin1Literal("themes/") % d->themeName % QLatin1Literal("/metadata.desktop")));
        VDesktopFile metadata(metadataPath);
        return metadata.value("X-Hawaii-Theme-Website", QUrl("http://www.maui-project.org/")).toUrl();
    }

    int Theme::toolTipDelay() const
    {
        return d->toolTipDelay;
    }
}

#include "moc_theme.cpp"
