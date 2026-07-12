#include "ui/MainWindow.h"

#include "core/LibraryScanner.h"
#include "platform/PlatformInfo.h"
#include "storage/LegacySettingsImporter.h"
#include "storage/MetadataStore.h"
#include "ui/MetadataDialog.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace {

enum SoundPadRole {
    RelativePathRole = Qt::UserRole,
    TitleRole,
    CategoryRole,
    ShortcutRole,
    DurationRole,
    FavoriteRole,
    PlayingRole,
    MissingRole,
    ProgressRole,
};

QIcon cueletIcon(const QString& name)
{
    return QIcon(QStringLiteral(":/resources/icons/%1.svg").arg(name));
}

QListWidgetItem* sidebarItem(const QIcon& icon, const QString& text)
{
    auto* item = new QListWidgetItem(icon, text);
    item->setSizeHint(QSize(172, 36));
    return item;
}

QListWidgetItem* sidebarItem(const QIcon& icon, const QString& text, int count)
{
    return sidebarItem(icon, count > 0 ? QStringLiteral("%1  %2").arg(text).arg(count) : text);
}

QAction* toolbarAction(QToolBar* toolbar,
                       const QIcon& icon,
                       const QString& text,
                       const QString& tooltip,
                       const QKeySequence& shortcut = QKeySequence())
{
    auto* action = toolbar->addAction(icon, text);
    action->setToolTip(tooltip);
    action->setStatusTip(tooltip);
    action->setWhatsThis(tooltip);
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    return action;
}

QAction* appAction(QObject* parent,
                   const QIcon& icon,
                   const QString& text,
                   const QString& tooltip,
                   const QKeySequence& shortcut = QKeySequence())
{
    auto* action = new QAction(icon, text, parent);
    action->setToolTip(tooltip);
    action->setStatusTip(tooltip);
    action->setWhatsThis(tooltip);
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    return action;
}

QToolButton* toolbarMenuButton(QToolBar* toolbar, const QIcon& icon, const QString& text, QMenu* menu)
{
    auto* button = new QToolButton(toolbar);
    button->setObjectName("ToolbarMenuButton");
    button->setIcon(icon);
    button->setText(text);
    button->setToolTip(text);
    button->setAccessibleName(text);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setMenu(menu);
    toolbar->addWidget(button);
    return button;
}

QFrame* divider(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setObjectName("SidebarDivider");
    line->setFrameShape(QFrame::HLine);
    return line;
}

QString cssColor(const QColor& color)
{
    return QStringLiteral("rgb(%1, %2, %3)").arg(color.red()).arg(color.green()).arg(color.blue());
}

QString cssRgba(const QColor& color, int alpha)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(alpha);
}

QLabel* sectionTitle(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName("SectionTitle");
    return label;
}

QFrame* settingsCard(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName("SettingsPanel");
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(12);
    return frame;
}

QVBoxLayout* cardLayout(QFrame* frame)
{
    return qobject_cast<QVBoxLayout*>(frame->layout());
}

QString boolStatus(bool value)
{
    return value ? QObject::tr("On") : QObject::tr("Off");
}

QString formatDuration(qint64 milliseconds)
{
    if (milliseconds <= 0) {
        return QObject::tr("--:--");
    }

    const qint64 totalSeconds = milliseconds / 1000;
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

QColor withAlpha(const QColor& color, int alpha)
{
    QColor result = color;
    result.setAlpha(alpha);
    return result;
}

class SoundPadDelegate : public QStyledItemDelegate {
public:
    explicit SoundPadDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        Q_UNUSED(option);
        if (!index.data(RelativePathRole).isValid()) {
            return QSize(480, 88);
        }
        return QSize(226, 148);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const bool isEmptyMessage = !index.data(RelativePathRole).isValid();
        if (isEmptyMessage) {
            drawEmptyMessage(painter, option, index);
            painter->restore();
            return;
        }

        const QPalette palette = option.palette;
        const QColor text = palette.color(QPalette::Text);
        const QColor muted = palette.color(QPalette::Disabled, QPalette::Text);
        const QColor base = palette.color(QPalette::Base);
        const QColor window = palette.color(QPalette::Window);
        const QColor accent = palette.color(QPalette::Highlight);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hover = option.state.testFlag(QStyle::State_MouseOver);
        const bool pressed = option.state.testFlag(QStyle::State_Sunken);
        const bool playing = index.data(PlayingRole).toBool();
        const bool favorite = index.data(FavoriteRole).toBool();
        const bool missing = index.data(MissingRole).toBool();

        QRectF card = option.rect.adjusted(5, 6, -5, -6);
        if (pressed) {
            card.translate(0, 1);
        }

        QColor fill = base;
        if (hover) {
            fill = fill.lighter(112);
        }
        if (selected) {
            fill = withAlpha(accent, 48);
        }
        if (playing) {
            fill = withAlpha(accent, 38);
        }
        if (missing) {
            fill = window.darker(103);
        }

        QColor stroke = palette.color(QPalette::Mid);
        if (selected || hover || playing) {
            stroke = selected || playing ? accent : withAlpha(accent, 130);
        }

        QPainterPath cardPath;
        cardPath.addRoundedRect(card, 11, 11);
        painter->fillPath(cardPath, fill);
        painter->setPen(QPen(stroke, playing ? 1.6 : 1.0));
        painter->drawPath(cardPath);

        QRectF artRect(card.left() + 14, card.top() + 13, card.width() - 28, 44);
        drawWaveform(painter, artRect, playing ? accent : muted, playing);

        if (favorite) {
            drawStar(painter, QRectF(card.right() - 30, card.top() + 12, 16, 16), accent);
        }

        const QString duration = index.data(DurationRole).toString();
        const QString shortcut = index.data(ShortcutRole).toString();
        drawBadge(painter,
                  QRectF(card.right() - 66, card.top() + 40, 50, 20),
                  duration.isEmpty() ? shortcut : duration,
                  text,
                  palette.color(QPalette::AlternateBase));

        QRectF titleRect(card.left() + 14, card.top() + 66, card.width() - 28, 32);
        QFont titleFont = option.font;
        titleFont.setPointSize(titleFont.pointSize() + 1);
        titleFont.setWeight(QFont::DemiBold);
        painter->setFont(titleFont);
        painter->setPen(missing ? muted : text);
        const QString title = index.data(TitleRole).toString();
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, option.fontMetrics.elidedText(title, Qt::ElideRight, titleRect.width()));

        const QString category = index.data(CategoryRole).toString();
        QFont badgeFont = option.font;
        badgeFont.setPointSize(qMax(8, badgeFont.pointSize() - 1));
        badgeFont.setWeight(QFont::DemiBold);
        painter->setFont(badgeFont);
        const QFontMetrics badgeMetrics(badgeFont);
        const int categoryWidth = qMin(static_cast<int>(card.width() - 92), badgeMetrics.horizontalAdvance(category) + 20);
        drawBadge(painter,
                  QRectF(card.left() + 14, card.bottom() - 32, categoryWidth, 22),
                  category,
                  muted,
                  palette.color(QPalette::AlternateBase));

        drawBadge(painter,
                  QRectF(card.right() - 66, card.bottom() - 32, 50, 22),
                  shortcut,
                  muted,
                  palette.color(QPalette::AlternateBase));

        if (playing) {
            const int progress = qBound(8, index.data(ProgressRole).toInt(), 100);
            QRectF progressTrack(card.left() + 1, card.bottom() - 4, card.width() - 2, 3);
            QRectF progressFill(progressTrack.left(), progressTrack.top(), progressTrack.width() * progress / 100.0, progressTrack.height());
            painter->fillRect(progressTrack, withAlpha(accent, 45));
            painter->fillRect(progressFill, accent);
        }

        painter->restore();
    }

private:
    static void drawBadge(QPainter* painter, const QRectF& rect, const QString& text, const QColor& color, const QColor& fill)
    {
        if (text.isEmpty()) {
            return;
        }
        QPainterPath path;
        path.addRoundedRect(rect, rect.height() / 2.0, rect.height() / 2.0);
        painter->fillPath(path, withAlpha(fill, 170));
        painter->setPen(color);
        painter->drawText(rect.adjusted(8, 0, -8, 0), Qt::AlignCenter, painter->fontMetrics().elidedText(text, Qt::ElideRight, rect.width() - 12));
    }

    static void drawEmptyMessage(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index)
    {
        QRectF rect = option.rect.adjusted(12, 12, -12, -12);
        painter->setPen(option.palette.color(QPalette::Disabled, QPalette::Text));
        painter->drawText(rect, Qt::AlignCenter | Qt::TextWordWrap, index.data(Qt::DisplayRole).toString());
    }

    static void drawWaveform(QPainter* painter, const QRectF& rect, const QColor& color, bool playing)
    {
        painter->save();
        QPen pen(withAlpha(color, playing ? 230 : 150), 3.0, Qt::SolidLine, Qt::RoundCap);
        painter->setPen(pen);
        const QVector<qreal> bars{0.32, 0.58, 0.42, 0.78, 0.55, 0.88, 0.46, 0.68, 0.36, 0.52};
        const qreal step = rect.width() / bars.size();
        for (int i = 0; i < bars.size(); ++i) {
            const qreal height = rect.height() * bars.at(i);
            const qreal x = rect.left() + step * i + step / 2.0;
            const qreal y1 = rect.center().y() - height / 2.0;
            const qreal y2 = rect.center().y() + height / 2.0;
            painter->drawLine(QPointF(x, y1), QPointF(x, y2));
        }
        if (playing) {
            QPainterPath play;
            const QRectF circle(rect.left(), rect.top() + 8, 28, 28);
            painter->setBrush(withAlpha(color, 210));
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(circle);
            play.moveTo(circle.left() + 11, circle.top() + 8);
            play.lineTo(circle.left() + 11, circle.bottom() - 8);
            play.lineTo(circle.right() - 8, circle.center().y());
            play.closeSubpath();
            painter->fillPath(play, Qt::white);
        }
        painter->restore();
    }

    static void drawStar(QPainter* painter, const QRectF& rect, const QColor& color)
    {
        QPainterPath star;
        const QPointF center = rect.center();
        const qreal outer = rect.width() / 2.0;
        const qreal inner = outer * 0.48;
        constexpr qreal pi = 3.14159265358979323846;
        for (int i = 0; i < 10; ++i) {
            const qreal angle = -pi / 2.0 + i * pi / 5.0;
            const qreal radius = i % 2 == 0 ? outer : inner;
            const QPointF point(center.x() + std::cos(angle) * radius, center.y() + std::sin(angle) * radius);
            i == 0 ? star.moveTo(point) : star.lineTo(point);
        }
        star.closeSubpath();
        painter->fillPath(star, color);
    }
};

QString matchingAudioDeviceId(const QVector<AudioOutputDevice>& devices, const QString& legacyName)
{
    const QString needle = legacyName.trimmed();
    if (needle.isEmpty()) {
        return {};
    }
    for (const AudioOutputDevice& device : devices) {
        if (QString::compare(device.description, needle, Qt::CaseInsensitive) == 0) {
            return device.id;
        }
    }
    for (const AudioOutputDevice& device : devices) {
        if (device.description.contains(needle, Qt::CaseInsensitive) || needle.contains(device.description, Qt::CaseInsensitive)) {
            return device.id;
        }
    }
    return {};
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUi();
    setupActions();
    applyStyle();

    m_audio.setVolume(m_settings.volume());
    m_audio.setAllowMultiplePlayback(m_settings.allowMultiplePlayback());
    m_audio.setOutputDeviceId(m_settings.audioOutputDeviceId());
    connect(&m_audio, &QtAudioService::errorOccurred, this, [this](const QString& message) {
        showTransientStatus(message, 7000);
    });
    connect(&m_audio, &QtAudioService::playbackIdle, this, [this]() {
        if (m_playingRelativePath.isEmpty()) {
            return;
        }
        m_playingRelativePath.clear();
        if (m_playbackPulseTimer) {
            m_playbackPulseTimer->stop();
        }
        refreshSoundList();
        updateActionStates();
    });

    attemptAutomaticLegacyImport();
    loadRememberedLibrary();
}

void MainWindow::setupUi()
{
    setWindowTitle(tr("Cuelet"));
    setMinimumSize(980, 640);
    setAcceptDrops(true);
    setUnifiedTitleAndToolBarOnMac(true);

    m_sidebarPanel = new QWidget(this);
    m_sidebarPanel->setObjectName("SidebarPanel");
    auto* sidebarLayout = new QVBoxLayout(m_sidebarPanel);
    sidebarLayout->setContentsMargins(10, 14, 10, 12);
    sidebarLayout->setSpacing(8);

    auto* sidebarHeading = new QLabel(tr("Soundboard"), m_sidebarPanel);
    sidebarHeading->setObjectName("SidebarHeading");
    sidebarLayout->addWidget(sidebarHeading);

    m_sidebar = new QListWidget(m_sidebarPanel);
    m_sidebar->setObjectName("Sidebar");
    m_sidebar->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_sidebar->setFrameShape(QFrame::NoFrame);
    m_sidebar->setMinimumWidth(174);
    m_sidebar->setMaximumWidth(260);
    m_sidebar->setFixedHeight(256);
    m_sidebar->setSpacing(4);
    m_sidebar->setIconSize(QSize(18, 18));
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebar->addItem(sidebarItem(cueletIcon("library"), tr("Library")));
    m_sidebar->addItem(sidebarItem(cueletIcon("star"), tr("Favorites")));
    m_sidebar->addItem(sidebarItem(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Recent")));
    m_sidebar->addItem(sidebarItem(cueletIcon("tags"), tr("Categories")));
    m_sidebar->addItem(sidebarItem(style()->standardIcon(QStyle::SP_DesktopIcon), tr("Profiles")));
    m_sidebar->addItem(sidebarItem(style()->standardIcon(QStyle::SP_TitleBarShadeButton), tr("Overlay")));
    m_sidebar->setCurrentRow(LibraryRow);
    connect(m_sidebar, &QListWidget::currentRowChanged, this, &MainWindow::handleSidebarChanged);
    sidebarLayout->addWidget(m_sidebar);

    sidebarLayout->addWidget(divider(m_sidebarPanel));

    auto* quickLabel = new QLabel(tr("Tips"), m_sidebarPanel);
    quickLabel->setObjectName("SidebarSectionLabel");
    sidebarLayout->addWidget(quickLabel);

    auto* hotkeyHint = new QLabel(tr("Space play selected\nEsc stop all\nCmd/Ctrl+F search"), m_sidebarPanel);
    hotkeyHint->setObjectName("SidebarHint");
    hotkeyHint->setWordWrap(true);
    sidebarLayout->addWidget(hotkeyHint);

    sidebarLayout->addStretch(1);

    m_sidebarSettingsButton = new QPushButton(cueletIcon("settings"), tr("Settings"), m_sidebarPanel);
    m_sidebarSettingsButton->setObjectName("SidebarSettingsButton");
    m_sidebarSettingsButton->setToolTip(tr("Open Cuelet settings"));
    m_sidebarSettingsButton->setAccessibleName(tr("Open settings"));
    connect(m_sidebarSettingsButton, &QPushButton::clicked, this, [this]() {
        m_sidebar->clearSelection();
        refreshSettingsPage();
        m_stack->setCurrentWidget(m_settingsPage);
    });
    sidebarLayout->addWidget(m_sidebarSettingsButton);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName("ContentStack");

    m_emptyPage = new QWidget(this);
    m_emptyPage->setObjectName("Canvas");
    auto* emptyLayout = new QVBoxLayout(m_emptyPage);
    emptyLayout->setContentsMargins(56, 48, 56, 48);
    emptyLayout->setSpacing(12);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->addStretch(1);

    auto* emptyDropZone = new QFrame(m_emptyPage);
    emptyDropZone->setObjectName("EmptyDropZone");
    emptyDropZone->setMinimumSize(520, 340);
    emptyDropZone->setMaximumWidth(620);
    auto* dropLayout = new QVBoxLayout(emptyDropZone);
    dropLayout->setContentsMargins(34, 30, 34, 30);
    dropLayout->setSpacing(12);
    dropLayout->setAlignment(Qt::AlignCenter);

    auto* padIllustration = new QFrame(emptyDropZone);
    padIllustration->setObjectName("PadIllustration");
    auto* padGrid = new QGridLayout(padIllustration);
    padGrid->setContentsMargins(10, 10, 10, 10);
    padGrid->setSpacing(6);
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 3; ++column) {
            auto* pad = new QLabel(padIllustration);
            pad->setObjectName(row == 0 && column == 1 ? "IllustrationPadActive" : "IllustrationPad");
            pad->setFixedSize(34, 28);
            padGrid->addWidget(pad, row, column);
        }
    }
    dropLayout->addWidget(padIllustration, 0, Qt::AlignCenter);

    m_emptyTitleLabel = new QLabel(tr("No library selected yet"), m_emptyPage);
    m_emptyTitleLabel->setObjectName("EmptyTitle");
    m_emptyTitleLabel->setAlignment(Qt::AlignCenter);

    m_emptyBodyLabel = new QLabel(tr("Choose a folder of audio clips, or drag one here to build your soundboard."), m_emptyPage);
    m_emptyBodyLabel->setObjectName("EmptyBody");
    m_emptyBodyLabel->setAlignment(Qt::AlignCenter);
    m_emptyBodyLabel->setMinimumWidth(420);
    m_emptyBodyLabel->setMaximumWidth(560);
    m_emptyBodyLabel->setMinimumHeight(48);
    m_emptyBodyLabel->setWordWrap(true);

    auto* emptyButton = new QPushButton(cueletIcon("folder-plus"), tr("Choose Library"), m_emptyPage);
    emptyButton->setObjectName("PrimaryButton");
    emptyButton->setMinimumWidth(154);
    emptyButton->setAccessibleName(tr("Choose a sound library folder"));
    connect(emptyButton, &QPushButton::clicked, this, &MainWindow::chooseLibrary);

    auto* secondaryActions = new QHBoxLayout;
    secondaryActions->setSpacing(10);
    auto* importEmptyButton = new QPushButton(cueletIcon("import"), tr("Import Sounds"), m_emptyPage);
    importEmptyButton->setObjectName("SecondaryButton");
    importEmptyButton->setAccessibleName(tr("Import individual audio files"));
    connect(importEmptyButton, &QPushButton::clicked, this, &MainWindow::importFiles);
    auto* recentEmptyButton = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Open recent library"), m_emptyPage);
    recentEmptyButton->setObjectName("SecondaryButton");
    recentEmptyButton->setAccessibleName(tr("Open recent library"));
    connect(recentEmptyButton, &QPushButton::clicked, this, [this]() {
        openLibraryFolder(m_settings.libraryFolder(), true);
    });
    secondaryActions->addStretch(1);
    secondaryActions->addWidget(importEmptyButton);
    secondaryActions->addWidget(recentEmptyButton);
    secondaryActions->addStretch(1);

    auto* emptyExplanation = new QLabel(tr("Tip: drag an audio folder here to open it as your soundboard."), m_emptyPage);
    emptyExplanation->setObjectName("EmptyExplanation");
    emptyExplanation->setAlignment(Qt::AlignCenter);
    emptyExplanation->setMaximumWidth(520);
    emptyExplanation->setMinimumHeight(28);
    emptyExplanation->setWordWrap(true);

    m_emptyRecentLabel = new QLabel(m_emptyPage);
    m_emptyRecentLabel->setObjectName("RecentLabel");
    m_emptyRecentLabel->setAlignment(Qt::AlignCenter);
    m_emptyRecentLabel->setMaximumWidth(560);
    m_emptyRecentLabel->setMinimumHeight(36);
    m_emptyRecentLabel->setWordWrap(true);

    dropLayout->addWidget(m_emptyTitleLabel, 0, Qt::AlignCenter);
    dropLayout->addWidget(m_emptyBodyLabel, 0, Qt::AlignCenter);
    dropLayout->addSpacing(8);
    dropLayout->addWidget(emptyButton, 0, Qt::AlignCenter);
    dropLayout->addLayout(secondaryActions);
    dropLayout->addSpacing(6);
    dropLayout->addWidget(emptyExplanation, 0, Qt::AlignCenter);
    dropLayout->addWidget(m_emptyRecentLabel, 0, Qt::AlignCenter);

    emptyLayout->addWidget(emptyDropZone, 0, Qt::AlignCenter);
    emptyLayout->addStretch(2);

    m_libraryPage = new QWidget(this);
    m_libraryPage->setObjectName("Canvas");
    auto* libraryLayout = new QVBoxLayout(m_libraryPage);
    libraryLayout->setContentsMargins(28, 22, 28, 18);
    libraryLayout->setSpacing(14);

    auto* pageTitle = new QLabel(tr("Sound Library"), m_libraryPage);
    pageTitle->setObjectName("PageTitle");
    libraryLayout->addWidget(pageTitle);

    m_libraryPathLabel = new QLabel(m_libraryPage);
    m_libraryPathLabel->setObjectName("PathLabel");
    m_libraryPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_libraryPathLabel->setWordWrap(true);
    libraryLayout->addWidget(m_libraryPathLabel);

    m_nowPlayingStrip = new QFrame(m_libraryPage);
    m_nowPlayingStrip->setObjectName("NowPlayingStrip");
    auto* nowPlayingLayout = new QHBoxLayout(m_nowPlayingStrip);
    nowPlayingLayout->setContentsMargins(12, 8, 8, 8);
    nowPlayingLayout->setSpacing(10);
    m_nowPlayingLabel = new QLabel(m_nowPlayingStrip);
    m_nowPlayingLabel->setObjectName("NowPlayingLabel");
    m_nowPlayingLabel->setWordWrap(false);
    m_nowPlayingLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_nowPlayingStopButton = new QPushButton(cueletIcon("stop"), tr("Stop All"), m_nowPlayingStrip);
    m_nowPlayingStopButton->setObjectName("NowPlayingStopButton");
    m_nowPlayingStopButton->setAccessibleName(tr("Stop all sounds"));
    connect(m_nowPlayingStopButton, &QPushButton::clicked, this, &MainWindow::stopPlayback);
    nowPlayingLayout->addWidget(m_nowPlayingLabel, 1);
    nowPlayingLayout->addWidget(m_nowPlayingStopButton, 0, Qt::AlignRight);
    m_nowPlayingStrip->hide();
    libraryLayout->addWidget(m_nowPlayingStrip);

    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(10);
    m_searchEdit = new QLineEdit(m_libraryPage);
    m_searchEdit->setObjectName("SearchField");
    m_searchEdit->setPlaceholderText(tr("Search sounds, notes, aliases, or categories"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->addAction(cueletIcon("search"), QLineEdit::LeadingPosition);
    m_searchEdit->setAccessibleName(tr("Search sounds"));
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::refreshSoundList);

    m_categoryCombo = new QComboBox(m_libraryPage);
    m_categoryCombo->setObjectName("FilterCombo");
    m_categoryCombo->setMinimumWidth(190);
    m_categoryCombo->setAccessibleName(tr("Filter by category"));
    connect(m_categoryCombo, &QComboBox::currentIndexChanged, this, &MainWindow::refreshSoundList);

    m_statusLabel = new QLabel(m_libraryPage);
    m_statusLabel->setObjectName("CountPill");
    m_statusLabel->setAlignment(Qt::AlignCenter);

    topRow->addWidget(m_searchEdit, 1);
    topRow->addWidget(m_categoryCombo);
    topRow->addWidget(m_statusLabel);
    libraryLayout->addLayout(topRow);

    m_soundList = new QListWidget(m_libraryPage);
    m_soundList->setObjectName("SoundGrid");
    m_soundList->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_soundList->setFrameShape(QFrame::NoFrame);
    m_soundList->setViewMode(QListView::IconMode);
    m_soundList->setResizeMode(QListView::Adjust);
    m_soundList->setMovement(QListView::Snap);
    m_soundList->setDragDropMode(QAbstractItemView::InternalMove);
    m_soundList->setDefaultDropAction(Qt::MoveAction);
    m_soundList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_soundList->setUniformItemSizes(true);
    m_soundList->setGridSize(QSize(246, 166));
    m_soundList->setIconSize(QSize(30, 30));
    m_soundList->setSpacing(12);
    m_soundList->setTextElideMode(Qt::ElideRight);
    m_soundList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_soundList->setWordWrap(false);
    m_soundList->setItemDelegate(new SoundPadDelegate(m_soundList));
    m_soundList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_soundList->setAccessibleName(tr("Sound pads"));
    connect(m_soundList, &QListWidget::itemDoubleClicked, this, &MainWindow::playSelectedSound);
    connect(m_soundList, &QListWidget::itemSelectionChanged, this, &MainWindow::updateActionStates);
    connect(m_soundList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        const std::optional<SoundClip> clip = selectedClip();
        if (!clip.has_value()) {
            return;
        }

        QMenu menu(this);
        auto* play = menu.addAction(cueletIcon("play"), tr("Play"));
        auto* edit = menu.addAction(cueletIcon("edit"), tr("Edit Clip"));
        auto* favorite = menu.addAction(cueletIcon("star"), clip->metadata.favorite ? tr("Remove Favorite") : tr("Add Favorite"));
        auto* reveal = menu.addAction(tr("Reveal in Folder"));
        menu.addSeparator();
        auto* deleteAction = menu.addAction(tr("Delete from Library..."));
        deleteAction->setEnabled(false);
        deleteAction->setToolTip(tr("Deleting files from Cuelet is not implemented yet."));

        QAction* chosen = menu.exec(m_soundList->viewport()->mapToGlobal(position));
        if (chosen == play) {
            playSelectedSound();
        } else if (chosen == edit) {
            editSelectedSound();
        } else if (chosen == favorite) {
            SoundMetadata metadata = clip->metadata;
            metadata.favorite = !metadata.favorite;
            if (!m_library.updateMetadata(clip->relativePath, metadata)) {
                QMessageBox::warning(this, tr("Favorite Sound"), m_library.lastError());
                return;
            }
            refreshCategories();
            refreshSoundList();
        } else if (chosen == reveal) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(clip->filePath).absolutePath()));
        }
    });
    libraryLayout->addWidget(m_soundList, 1);

    m_playbackPulseTimer = new QTimer(this);
    m_playbackPulseTimer->setInterval(220);
    connect(m_playbackPulseTimer, &QTimer::timeout, this, [this]() {
        m_playbackPulse = (m_playbackPulse + 7) % 101;
        for (int row = 0; row < m_soundList->count(); ++row) {
            QListWidgetItem* item = m_soundList->item(row);
            if (item && item->data(PlayingRole).toBool()) {
                item->setData(ProgressRole, m_playbackPulse);
            }
        }
        m_soundList->viewport()->update();
    });

    m_settingsPage = new QWidget(this);
    m_settingsPage->setObjectName("Canvas");
    auto* settingsRootLayout = new QVBoxLayout(m_settingsPage);
    settingsRootLayout->setContentsMargins(0, 0, 0, 0);

    auto* settingsScroll = new QScrollArea(m_settingsPage);
    settingsScroll->setObjectName("SettingsScroll");
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setFrameShape(QFrame::NoFrame);

    auto* settingsContent = new QWidget(settingsScroll);
    settingsContent->setObjectName("Canvas");
    auto* settingsLayout = new QVBoxLayout(settingsContent);
    settingsLayout->setContentsMargins(28, 22, 28, 22);
    settingsLayout->setSpacing(14);

    auto* settingsTitle = new QLabel(tr("Settings"), settingsContent);
    settingsTitle->setObjectName("PageTitle");
    settingsLayout->addWidget(settingsTitle);

    auto* libraryCard = settingsCard(settingsContent);
    auto* libraryCardLayout = cardLayout(libraryCard);
    libraryCardLayout->addWidget(sectionTitle(tr("Library"), libraryCard));
    m_settingsLibraryLabel = new QLabel(libraryCard);
    m_settingsLibraryLabel->setObjectName("PathLabel");
    m_settingsLibraryLabel->setWordWrap(true);
    auto* settingsChooseButton = new QPushButton(cueletIcon("folder-plus"), tr("Choose Library"), libraryCard);
    connect(settingsChooseButton, &QPushButton::clicked, this, &MainWindow::chooseLibrary);
    auto* legacyImportButton = new QPushButton(cueletIcon("import"), tr("Import Legacy Config"), libraryCard);
    connect(legacyImportButton, &QPushButton::clicked, this, &MainWindow::importLegacyConfig);
    auto* libraryRow = new QHBoxLayout;
    libraryRow->setSpacing(12);
    libraryRow->addWidget(m_settingsLibraryLabel, 1);
    libraryRow->addWidget(settingsChooseButton);
    libraryRow->addWidget(legacyImportButton);
    libraryCardLayout->addLayout(libraryRow);
    settingsLayout->addWidget(libraryCard);

    auto* playbackCard = settingsCard(settingsContent);
    auto* playbackLayout = cardLayout(playbackCard);
    playbackLayout->addWidget(sectionTitle(tr("Playback"), playbackCard));
    auto* volumeRow = new QHBoxLayout;
    auto* volumeLabel = new QLabel(tr("Volume"), playbackCard);
    volumeLabel->setObjectName("FieldLabel");
    m_volumeValueLabel = new QLabel(playbackCard);
    m_volumeValueLabel->setObjectName("PillLabel");
    volumeRow->addWidget(volumeLabel);
    volumeRow->addStretch(1);
    volumeRow->addWidget(m_volumeValueLabel);
    playbackLayout->addLayout(volumeRow);
    m_volumeSlider = new QSlider(Qt::Horizontal, playbackCard);
    m_volumeSlider->setRange(0, 100);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        const double volume = value / 100.0;
        m_audio.setVolume(volume);
        m_settings.setVolume(volume);
        m_volumeValueLabel->setText(tr("%1%").arg(value));
    });
    playbackLayout->addWidget(m_volumeSlider);
    m_multiplePlaybackCheck = new QCheckBox(tr("Allow multiple sounds at once"), playbackCard);
    connect(m_multiplePlaybackCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_audio.setAllowMultiplePlayback(checked);
        m_settings.setAllowMultiplePlayback(checked);
    });
    playbackLayout->addWidget(m_multiplePlaybackCheck);
    auto* stopAllButton = new QPushButton(cueletIcon("stop"), tr("Stop All Sounds"), playbackCard);
    connect(stopAllButton, &QPushButton::clicked, this, &MainWindow::stopPlayback);
    playbackLayout->addWidget(stopAllButton, 0, Qt::AlignLeft);
    settingsLayout->addWidget(playbackCard);

    auto* displayCard = settingsCard(settingsContent);
    auto* displayLayout = cardLayout(displayCard);
    displayLayout->addWidget(sectionTitle(tr("Display"), displayCard));
    m_showExtensionsCheck = new QCheckBox(tr("Show file extensions in sound titles"), displayCard);
    connect(m_showExtensionsCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings.setShowFileExtensions(checked);
        refreshSoundList();
    });
    displayLayout->addWidget(m_showExtensionsCheck);
    auto* appearanceNote = new QLabel(tr("Theme follows the system appearance. Accent color follows the platform highlight color."), displayCard);
    appearanceNote->setObjectName("MutedLabel");
    appearanceNote->setWordWrap(true);
    displayLayout->addWidget(appearanceNote);
    settingsLayout->addWidget(displayCard);

    auto* hotkeysCard = settingsCard(settingsContent);
    auto* hotkeysLayout = cardLayout(hotkeysCard);
    hotkeysLayout->addWidget(sectionTitle(tr("Hotkeys"), hotkeysCard));
    auto* hotkeysNote = new QLabel(tr("Current shortcuts: Space or Return plays the selected pad, Esc stops all sounds, and Cmd/Ctrl+F searches. Global Stop All, per-sound hotkeys, and overlay show/hide shortcuts are planned."), hotkeysCard);
    hotkeysNote->setObjectName("MutedLabel");
    hotkeysNote->setWordWrap(true);
    hotkeysLayout->addWidget(hotkeysNote);
    settingsLayout->addWidget(hotkeysCard);

    auto* overlayCard = settingsCard(settingsContent);
    auto* overlayLayout = cardLayout(overlayCard);
    overlayLayout->addWidget(sectionTitle(tr("Overlay"), overlayCard));
    auto* overlayNote = new QLabel(tr("Planned compact mode: 3x3 or 4x4 floating grid, active profile selector, opacity, corner/center positions, show/hide shortcut, and a persistent Stop All control."), overlayCard);
    overlayNote->setObjectName("MutedLabel");
    overlayNote->setWordWrap(true);
    overlayLayout->addWidget(overlayNote);
    settingsLayout->addWidget(overlayCard);

    auto* importBehaviorCard = settingsCard(settingsContent);
    auto* importBehaviorLayout = cardLayout(importBehaviorCard);
    importBehaviorLayout->addWidget(sectionTitle(tr("Import Behavior"), importBehaviorCard));
    auto* importBehaviorNote = new QLabel(tr("Cuelet scans subfolders and supports mp3, wav, ogg, flac, and m4a files. Drag audio files onto an open library to copy them in, or drop a folder onto the empty state to open it as a library."), importBehaviorCard);
    importBehaviorNote->setObjectName("MutedLabel");
    importBehaviorNote->setWordWrap(true);
    importBehaviorLayout->addWidget(importBehaviorNote);
    settingsLayout->addWidget(importBehaviorCard);

    auto* audioCard = settingsCard(settingsContent);
    auto* audioLayout = cardLayout(audioCard);
    audioLayout->addWidget(sectionTitle(tr("Audio Output"), audioCard));
    auto* audioRow = new QHBoxLayout;
    m_outputDeviceCombo = new QComboBox(audioCard);
    m_outputDeviceCombo->setObjectName("FilterCombo");
    connect(m_outputDeviceCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QString id = m_outputDeviceCombo->itemData(index).toString();
        m_audio.setOutputDeviceId(id);
        m_settings.setAudioOutputDeviceId(id);
    });
    auto* refreshDevicesButton = new QPushButton(cueletIcon("refresh"), tr("Refresh"), audioCard);
    connect(refreshDevicesButton, &QPushButton::clicked, this, &MainWindow::refreshAudioDevices);
    audioRow->addWidget(m_outputDeviceCombo, 1);
    audioRow->addWidget(refreshDevicesButton);
    audioLayout->addLayout(audioRow);
    settingsLayout->addWidget(audioCard);

    auto* loudnessCard = settingsCard(settingsContent);
    auto* loudnessLayout = cardLayout(loudnessCard);
    loudnessLayout->addWidget(sectionTitle(tr("Loudness"), loudnessCard));
    m_loudnessCheck = new QCheckBox(tr("Loudness normalization"), loudnessCard);
    connect(m_loudnessCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings.setLoudnessNormalizationEnabled(checked);
        refreshSettingsPage();
    });
    loudnessLayout->addWidget(m_loudnessCheck);
    m_loudnessStatusLabel = new QLabel(loudnessCard);
    m_loudnessStatusLabel->setObjectName("MutedLabel");
    m_loudnessStatusLabel->setWordWrap(true);
    loudnessLayout->addWidget(m_loudnessStatusLabel);
    settingsLayout->addWidget(loudnessCard);

    auto* virtualMicCard = settingsCard(settingsContent);
    auto* virtualMicLayout = cardLayout(virtualMicCard);
    virtualMicLayout->addWidget(sectionTitle(tr("Microphone / Virtual Device"), virtualMicCard));
    m_virtualMicStatusLabel = new QLabel(virtualMicCard);
    m_virtualMicStatusLabel->setObjectName("MutedLabel");
    m_virtualMicStatusLabel->setWordWrap(true);
    virtualMicLayout->addWidget(m_virtualMicStatusLabel);
    auto* virtualMicNote = new QLabel(PlatformInfo::virtualMicrophoneNote(), virtualMicCard);
    virtualMicNote->setWordWrap(true);
    virtualMicNote->setObjectName("MutedLabel");
    virtualMicLayout->addWidget(virtualMicNote);
    settingsLayout->addWidget(virtualMicCard);

    auto* legacyCard = settingsCard(settingsContent);
    auto* legacyLayout = cardLayout(legacyCard);
    legacyLayout->addWidget(sectionTitle(tr("Legacy Import"), legacyCard));
    m_legacyStatusLabel = new QLabel(legacyCard);
    m_legacyStatusLabel->setObjectName("FieldLabel");
    legacyLayout->addWidget(m_legacyStatusLabel);
    m_legacySummaryLabel = new QLabel(legacyCard);
    m_legacySummaryLabel->setObjectName("MutedLabel");
    m_legacySummaryLabel->setWordWrap(true);
    legacyLayout->addWidget(m_legacySummaryLabel);
    settingsLayout->addWidget(legacyCard);

    auto* advancedCard = settingsCard(settingsContent);
    auto* advancedLayout = cardLayout(advancedCard);
    advancedLayout->addWidget(sectionTitle(tr("Advanced"), advancedCard));
    auto* advancedNote = new QLabel(tr("Metadata is stored beside the library as .cuelet-metadata.json so categories, favorites, aliases, notes, and future hotkeys can travel with the folder."), advancedCard);
    advancedNote->setObjectName("MutedLabel");
    advancedNote->setWordWrap(true);
    advancedLayout->addWidget(advancedNote);
    m_settingsPathLabel = new QLabel(advancedCard);
    m_settingsPathLabel->setObjectName("MutedLabel");
    m_settingsPathLabel->setWordWrap(true);
    advancedLayout->addWidget(m_settingsPathLabel);
    settingsLayout->addWidget(advancedCard);

    settingsLayout->addStretch(1);
    settingsScroll->setWidget(settingsContent);
    settingsRootLayout->addWidget(settingsScroll);

    m_stack->addWidget(m_emptyPage);
    m_stack->addWidget(m_libraryPage);
    m_stack->addWidget(m_settingsPage);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName("MainSplitter");
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_sidebarPanel);
    m_splitter->addWidget(m_stack);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({m_settings.sidebarWidth(), 900});
    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
        m_settings.setSidebarWidth(m_splitter->sizes().value(0, m_settings.sidebarWidth()));
    });
    setCentralWidget(m_splitter);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->hide();

    refreshSettingsPage();
    showEmptyPage();
    updateActionStates();
}

void MainWindow::setupActions()
{
    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName("MainToolbar");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setAllowedAreas(Qt::TopToolBarArea);
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto* chooseAction = appAction(this,
                                   cueletIcon("folder-plus"),
                                   tr("Choose Library"),
                                   tr("Choose a sound library folder"),
                                   QKeySequence::Open);
    connect(chooseAction, &QAction::triggered, this, &MainWindow::chooseLibrary);

    m_importAction = appAction(this,
                               cueletIcon("import"),
                               tr("Import Sounds"),
                               tr("Import audio files into the current library"));
    connect(m_importAction, &QAction::triggered, this, &MainWindow::importFiles);

    m_rescanAction = appAction(this,
                               cueletIcon("refresh"),
                               tr("Rescan Library"),
                               tr("Rescan the current library folder"));
    connect(m_rescanAction, &QAction::triggered, this, &MainWindow::rescanLibrary);

    auto* libraryMenu = new QMenu(toolbar);
    libraryMenu->addAction(chooseAction);
    libraryMenu->addAction(m_importAction);
    libraryMenu->addAction(m_rescanAction);
    toolbarMenuButton(toolbar, cueletIcon("library"), tr("Library"), libraryMenu);

    m_playAction = toolbarAction(toolbar,
                                 cueletIcon("play"),
                                 tr("Play Selected"),
                                 tr("Play the selected sound"),
                                 QKeySequence(Qt::Key_Space));
    connect(m_playAction, &QAction::triggered, this, &MainWindow::playSelectedSound);

    m_stopAction = toolbarAction(toolbar,
                                 cueletIcon("stop"),
                                 tr("Stop All"),
                                 tr("Stop all playback"),
                                 QKeySequence(Qt::Key_Escape));
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopPlayback);

    m_fadeOutAction = appAction(this,
                                cueletIcon("waveform"),
                                tr("Fade Out"),
                                tr("Fade out playback gracefully"));
    m_fadeOutAction->setEnabled(false);

    toolbar->addSeparator();

    m_renameAction = appAction(this,
                               cueletIcon("edit"),
                               tr("Rename"),
                               tr("Rename the selected sound"));
    connect(m_renameAction, &QAction::triggered, this, &MainWindow::editSelectedSound);

    m_editAction = appAction(this,
                             cueletIcon("edit"),
                             tr("Edit Clip"),
                             tr("Edit metadata for the selected sound"));
    connect(m_editAction, &QAction::triggered, this, &MainWindow::editSelectedSound);

    m_deleteAction = appAction(this,
                               style()->standardIcon(QStyle::SP_TrashIcon),
                               tr("Delete"),
                               tr("Delete the selected sounds"));
    m_deleteAction->setEnabled(false);

    auto* viewGroup = new QActionGroup(this);
    m_gridAction = appAction(this,
                             style()->standardIcon(QStyle::SP_FileDialogContentsView),
                             tr("Grid View"),
                             tr("Show sounds as large trigger pads"));
    m_gridAction->setCheckable(true);
    m_gridAction->setChecked(true);
    m_gridAction->setActionGroup(viewGroup);
    connect(m_gridAction, &QAction::triggered, this, [this]() {
        setSoundGridMode(true);
    });

    m_listAction = appAction(this,
                             style()->standardIcon(QStyle::SP_FileDialogDetailedView),
                             tr("List View"),
                             tr("Show sounds in a compact list"));
    m_listAction->setCheckable(true);
    m_listAction->setActionGroup(viewGroup);
    connect(m_listAction, &QAction::triggered, this, [this]() {
        setSoundGridMode(false);
    });

    auto* viewMenu = new QMenu(toolbar);
    viewMenu->addAction(m_gridAction);
    viewMenu->addAction(m_listAction);
    toolbarMenuButton(toolbar, style()->standardIcon(QStyle::SP_FileDialogContentsView), tr("View"), viewMenu);

    auto* searchAction = toolbarAction(toolbar,
                                       cueletIcon("search"),
                                       tr("Search"),
                                       tr("Focus the sound search field"),
                                       QKeySequence::Find);
    connect(searchAction, &QAction::triggered, this, &MainWindow::focusSearch);

    m_overlayAction = appAction(this,
                                style()->standardIcon(QStyle::SP_TitleBarShadeButton),
                                tr("Overlay"),
                                tr("Show the compact floating soundboard overlay"));
    m_overlayAction->setCheckable(true);
    m_overlayAction->setEnabled(false);

    auto* settingsAction = appAction(this,
                                     cueletIcon("settings"),
                                     tr("Settings"),
                                     tr("Open Cuelet settings"));
    connect(settingsAction, &QAction::triggered, this, [this]() {
        m_sidebar->clearSelection();
        refreshSettingsPage();
        m_stack->setCurrentWidget(m_settingsPage);
    });

    auto* advancedMenu = new QMenu(toolbar);
    advancedMenu->addAction(m_overlayAction);
    advancedMenu->addAction(settingsAction);
    advancedMenu->addSeparator();
    auto* legacyImportAction = advancedMenu->addAction(cueletIcon("import"), tr("Import Legacy Config"));
    legacyImportAction->setToolTip(tr("Import settings and metadata from the legacy soundboard"));
    connect(legacyImportAction, &QAction::triggered, this, &MainWindow::importLegacyConfig);
    toolbarMenuButton(toolbar, style()->standardIcon(QStyle::SP_TitleBarUnshadeButton), tr("More"), advancedMenu);

    auto* findAction = new QAction(tr("Find"), this);
    findAction->setShortcut(QKeySequence::Find);
    addAction(findAction);
    connect(findAction, &QAction::triggered, this, &MainWindow::focusSearch);

    menuBar()->setNativeMenuBar(true);
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(chooseAction);
    fileMenu->addAction(m_importAction);
    fileMenu->addAction(m_rescanAction);
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(tr("Quit Cuelet"));
    quitAction->setMenuRole(QAction::QuitRole);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(findAction);
    editMenu->addAction(m_renameAction);
    editMenu->addAction(m_editAction);
    editMenu->addAction(m_deleteAction);

    auto* playbackMenu = menuBar()->addMenu(tr("&Playback"));
    playbackMenu->addAction(m_playAction);
    playbackMenu->addAction(m_stopAction);
    playbackMenu->addAction(m_fadeOutAction);

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAction = helpMenu->addAction(tr("About Cuelet"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    auto* playSpace = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(playSpace, &QShortcut::activated, this, &MainWindow::playSelectedSound);
    auto* playReturn = new QShortcut(QKeySequence(Qt::Key_Return), this);
    connect(playReturn, &QShortcut::activated, this, &MainWindow::playSelectedSound);

    updateActionStates();
}

void MainWindow::applyStyle()
{
    const QPalette palette = QApplication::palette();
    const bool dark = palette.color(QPalette::Window).lightness() < 128;
    const QColor accent = palette.color(QPalette::Highlight);
    const QColor window = dark ? QColor("#1C1C1E") : QColor("#F5F5F7");
    const QColor toolbar = dark ? QColor("#242426") : QColor("#FAFAFC");
    const QColor sidebar = dark ? QColor("#202124") : QColor("#F2F2F4");
    const QColor panel = dark ? QColor("#2C2C2E") : QColor("#FFFFFF");
    const QColor text = dark ? QColor("#F5F5F7") : QColor("#1D1D1F");
    const QColor muted = dark ? QColor("#A1A1A6") : QColor("#6E6E73");
    const QColor border = dark ? QColor("#3A3A3C") : QColor("#D9D9DE");
    const QColor softBorder = dark ? QColor("#48484A") : QColor("#E5E5EA");

    setStyleSheet(QStringLiteral(R"(
        QMainWindow {
            background: %1;
            color: %5;
        }
        QWidget#Canvas, QStackedWidget#ContentStack {
            background: %1;
            color: %5;
        }
        QScrollArea#SettingsScroll {
            background: %1;
            border: 0;
        }
        QSplitter#MainSplitter::handle {
            background: %7;
            width: 1px;
        }
        QWidget#SidebarPanel {
            background: %3;
            border-right: 1px solid %7;
        }
        QLabel#SidebarHeading {
            color: %5;
            font-size: 12px;
            font-weight: 700;
            padding: 0 8px 4px 8px;
            text-transform: uppercase;
        }
        QLabel#SidebarSectionLabel {
            color: %14;
            font-size: 11px;
            font-weight: 700;
            padding: 2px 8px 0 8px;
            text-transform: uppercase;
        }
        QLabel#SidebarHint {
            color: %14;
            font-size: 12px;
            padding: 0 8px;
        }
        QFrame#SidebarDivider {
            border: 0;
            border-top: 1px solid %7;
            margin: 6px 8px;
        }
        QToolBar#MainToolbar {
            background: %2;
            border: 0;
            border-bottom: 1px solid %7;
            padding: 6px 10px;
            spacing: 6px;
        }
        QToolBar#MainToolbar QToolButton, QToolButton#ToolbarMenuButton {
            min-height: 29px;
            padding: 3px 10px;
            border: 1px solid transparent;
            border-radius: 7px;
            background: transparent;
            font-weight: 600;
        }
        QToolBar#MainToolbar QToolButton:hover, QToolButton#ToolbarMenuButton:hover {
            background: %9;
            border-color: %10;
        }
        QToolBar#MainToolbar QToolButton:pressed, QToolButton#ToolbarMenuButton:pressed {
            background: %11;
        }
        QToolBar#MainToolbar QToolButton:checked, QToolButton#ToolbarMenuButton:checked {
            background: %9;
            border-color: %10;
        }
        QToolBar#MainToolbar QToolButton:disabled, QToolButton#ToolbarMenuButton:disabled {
            color: %14;
            background: transparent;
            border-color: transparent;
            opacity: 0.45;
        }
        QToolBar#MainToolbar::separator {
            width: 1px;
            background: %7;
            margin: 7px 8px;
        }
        QListWidget#Sidebar {
            border: 0;
            padding: 0;
            background: %3;
            outline: none;
        }
        QListWidget#Sidebar::item {
            min-height: 31px;
            padding: 5px 9px;
            border-radius: 7px;
            color: %5;
        }
        QListWidget#Sidebar::item:selected {
            background: %9;
            color: %5;
            border: 1px solid %10;
        }
        QListWidget#Sidebar::item:hover:!selected {
            background: %12;
        }
        QPushButton#SidebarSettingsButton {
            min-height: 32px;
            padding: 4px 10px;
            text-align: left;
            border: 1px solid transparent;
            border-radius: 7px;
            background: transparent;
            color: %5;
            font-weight: 600;
        }
        QPushButton#SidebarSettingsButton:hover {
            background: %12;
            border-color: %7;
        }
        QListWidget#SoundGrid {
            border: 0;
            padding: 4px 0;
            background: %1;
            outline: none;
        }
        QListWidget#SoundGrid::item {
            border: 0;
            background: transparent;
            color: %5;
        }
        QListWidget#SoundGrid::item:selected {
            background: transparent;
        }
        QListWidget#SoundGrid::item:hover:!selected {
            background: transparent;
        }
        QLineEdit, QComboBox, QTextEdit {
            min-height: 32px;
            border: 1px solid %8;
            border-radius: 7px;
            padding: 3px 9px;
            background: %4;
            color: %5;
            selection-background-color: %6;
        }
        QLineEdit#SearchField {
            padding-left: 4px;
        }
        QLineEdit:focus, QComboBox:focus, QTextEdit:focus {
            border-color: %6;
        }
        QLineEdit:disabled, QComboBox:disabled, QPushButton:disabled {
            color: %14;
            background: %12;
            border-color: %7;
        }
        QComboBox::drop-down {
            border: 0;
            width: 24px;
        }
        QPushButton {
            min-height: 32px;
            padding: 4px 13px;
            border: 1px solid %8;
            border-radius: 7px;
            background: %4;
            color: %5;
        }
        QPushButton:hover {
            border-color: %10;
            background: %13;
        }
        QPushButton:pressed {
            background: %11;
        }
        QPushButton#PrimaryButton {
            border: 0;
            background: %6;
            color: white;
            font-weight: 600;
        }
        QPushButton#PrimaryButton:hover {
            background: %10;
        }
        QPushButton#SecondaryButton {
            min-height: 30px;
            padding: 3px 11px;
            background: transparent;
        }
        QFrame#EmptyDropZone {
            background: %15;
            border: 1px solid %8;
            border-radius: 12px;
        }
        QFrame#PadIllustration {
            background: %12;
            border: 1px solid %8;
            border-radius: 8px;
        }
        QLabel#IllustrationPad, QLabel#IllustrationPadActive {
            background: %4;
            border: 1px solid %8;
            border-radius: 5px;
        }
        QLabel#IllustrationPadActive {
            background: %9;
            border-color: %10;
        }
        QLabel#EmptyTitle, QLabel#PageTitle {
            color: %5;
            font-size: 24px;
            font-weight: 600;
        }
        QLabel#EmptyTitle {
            font-size: 26px;
        }
        QLabel#EmptyBody, QLabel#MutedLabel, QLabel#PathLabel {
            color: %14;
        }
        QLabel#EmptyBody, QLabel#EmptyExplanation, QLabel#RecentLabel {
            font-size: 14px;
        }
        QLabel#EmptyExplanation, QLabel#RecentLabel {
            color: %14;
            font-size: 12px;
        }
        QFrame#NowPlayingStrip {
            color: %5;
            background: %9;
            border: 1px solid %10;
            border-radius: 8px;
        }
        QLabel#NowPlayingLabel {
            color: %5;
            font-weight: 600;
        }
        QPushButton#NowPlayingStopButton {
            min-height: 26px;
            padding: 2px 10px;
            border: 1px solid %10;
            background: %11;
            font-weight: 700;
        }
        QLabel#CountPill {
            color: %14;
            background: %12;
            border: 1px solid %8;
            border-radius: 8px;
            padding: 6px 9px;
            font-size: 12px;
            font-weight: 600;
        }
        QLabel#SectionTitle {
            color: %5;
            font-size: 15px;
            font-weight: 600;
        }
        QLabel#FieldLabel {
            color: %5;
            font-weight: 600;
        }
        QLabel#PillLabel {
            color: %5;
            background: %12;
            border: 1px solid %8;
            border-radius: 8px;
            padding: 2px 8px;
            font-weight: 600;
        }
        QFrame#SettingsPanel {
            border: 1px solid %7;
            border-radius: 8px;
            background: %15;
        }
        QSlider::groove:horizontal {
            height: 4px;
            border-radius: 2px;
            background: %8;
        }
        QSlider::handle:horizontal {
            width: 18px;
            height: 18px;
            margin: -7px 0;
            border-radius: 9px;
            border: 1px solid %8;
            background: %4;
        }
        QSlider::sub-page:horizontal {
            border-radius: 2px;
            background: %6;
        }
        QStatusBar {
            background: %2;
            color: %14;
            border-top: 1px solid %7;
            min-height: 22px;
        }
    )")
                      .arg(cssColor(window),
                           cssColor(toolbar),
                           cssColor(sidebar),
                           cssColor(panel),
                           cssColor(text),
                           cssColor(accent),
                           cssColor(border),
                           cssColor(softBorder),
                           cssRgba(accent, dark ? 42 : 28),
                           cssRgba(accent, dark ? 92 : 70),
                           cssRgba(accent, dark ? 64 : 44),
                           cssRgba(text, dark ? 14 : 8),
                           dark ? cssColor(QColor("#323236")) : cssColor(QColor("#FBFBFD")),
                           cssColor(muted),
                           dark ? cssColor(QColor("#202023")) : cssColor(QColor("#F7F7FA"))));
}

void MainWindow::loadRememberedLibrary()
{
    const QString folder = m_settings.libraryFolder();
    if (!folder.isEmpty()) {
        openLibraryFolder(folder, false);
    }
}

void MainWindow::attemptAutomaticLegacyImport()
{
    if (m_settings.legacyMigrationAttempted()) {
        return;
    }

    const QStringList candidates = LegacySettingsImporter::likelyConfigFiles();
    if (candidates.isEmpty()) {
        m_settings.setLegacyMigrationAttempted(true);
        m_settings.setLegacyMigrationSummary(tr("No legacy soundboard config was found in standard config locations."));
        return;
    }

    applyLegacyImportFile(candidates.first(), true);
}

void MainWindow::applyLegacyImportFile(const QString& filePath, bool automatic)
{
    LegacySettingsImport legacy = LegacySettingsImporter::readFile(filePath);
    QStringList summary;
    summary.append(automatic ? tr("Automatic import checked: %1").arg(filePath) : tr("Manual import: %1").arg(filePath));

    if (!legacy.valid) {
        summary.append(tr("Skipped: %1").arg(legacy.error));
        m_settings.setLegacyMigrationAttempted(true);
        m_settings.setLegacyMigrationSourcePath(filePath);
        m_settings.setLegacyMigrationSummary(summary.join('\n'));
        refreshSettingsPage();
        return;
    }

    bool openedLibrary = false;
    const bool canSetLibrary = !legacy.libraryFolder.isEmpty() && (!automatic || !m_settings.hasLibraryFolder());
    if (canSetLibrary) {
        openLibraryFolder(legacy.libraryFolder, true);
        openedLibrary = m_library.hasLibrary();
        summary.append(openedLibrary ? tr("Imported library folder.") : tr("Library folder could not be opened."));
    } else if (!legacy.libraryFolder.isEmpty()) {
        summary.append(tr("Kept existing Cuelet library folder."));
    }

    if (legacy.showExtensions.has_value() && (!automatic || !m_settings.hasShowFileExtensionsSetting())) {
        m_settings.setShowFileExtensions(legacy.showExtensions.value());
        summary.append(tr("Imported show file extensions: %1.").arg(boolStatus(legacy.showExtensions.value())));
    }
    if (legacy.useLoudness.has_value() && (!automatic || !m_settings.hasLoudnessNormalizationSetting())) {
        m_settings.setLoudnessNormalizationEnabled(legacy.useLoudness.value());
        summary.append(tr("Preserved loudness normalization setting: %1.").arg(boolStatus(legacy.useLoudness.value())));
    }
    if (legacy.sidebarWidth.has_value() && (!automatic || !m_settings.hasSidebarWidthSetting())) {
        m_settings.setSidebarWidth(legacy.sidebarWidth.value());
        if (m_splitter) {
            m_splitter->setSizes({m_settings.sidebarWidth(), 900});
        }
        summary.append(tr("Imported sidebar width."));
    }
    if (!legacy.outputDeviceName.isEmpty() && (!automatic || !m_settings.hasAudioOutputDeviceId())) {
        const QString deviceId = matchingAudioDeviceId(m_audio.outputDevices(), legacy.outputDeviceName);
        if (!deviceId.isEmpty()) {
            m_settings.setAudioOutputDeviceId(deviceId);
            m_audio.setOutputDeviceId(deviceId);
            summary.append(tr("Matched legacy output device: %1.").arg(legacy.outputDeviceName));
        } else {
            summary.append(tr("Could not match legacy output device: %1.").arg(legacy.outputDeviceName));
        }
    }

    if (legacy.virtualMicEnabled.has_value()) {
        m_settings.setLegacyVirtualMicEnabled(legacy.virtualMicEnabled.value());
    }
    if (legacy.micLoopbackEnabled.has_value()) {
        m_settings.setLegacyMicLoopbackEnabled(legacy.micLoopbackEnabled.value());
    }
    if (!legacy.virtualMicOutputDevice.isEmpty()) {
        m_settings.setLegacyVirtualMicOutputDevice(legacy.virtualMicOutputDevice);
    }
    if (!legacy.virtualMicInputDevice.isEmpty()) {
        m_settings.setLegacyVirtualMicInputDevice(legacy.virtualMicInputDevice);
    }
    if (legacy.virtualMicEnabled.has_value() || legacy.micLoopbackEnabled.has_value()
        || !legacy.virtualMicOutputDevice.isEmpty() || !legacy.virtualMicInputDevice.isEmpty()) {
        summary.append(tr("Preserved legacy virtual microphone settings as inactive compatibility data."));
    }

    if (m_library.hasLibrary()) {
        QHash<QString, SoundMetadata> merged = LegacySettingsImporter::mergeMetadata(m_library.metadata(), legacy.metadataByPath);
        const QStringList favorites = LegacySettingsImporter::favoritePathsForLibrary(legacy, m_library.folder());
        merged = LegacySettingsImporter::mergeFavoritePaths(merged, favorites);
        if (!legacy.metadataByPath.isEmpty() || !favorites.isEmpty()) {
            if (m_library.mergeMetadata(merged)) {
                summary.append(tr("Merged %1 legacy metadata entries and %2 favorite paths.")
                                   .arg(legacy.metadataByPath.size())
                                   .arg(favorites.size()));
            } else {
                summary.append(tr("Could not save merged legacy metadata: %1").arg(m_library.lastError()));
            }
        }
        summary.append(legacy.notes);
    } else if (!legacy.favoritePaths.isEmpty() || !legacy.metadataByPath.isEmpty()) {
        summary.append(tr("Legacy favorites/metadata were found but no Cuelet library is open yet."));
    }

    m_settings.setLegacyMigrationAttempted(true);
    m_settings.setLegacyMigrationSourcePath(filePath);
    m_settings.setLegacyMigrationSummary(summary.join('\n'));
    refreshCategories();
    refreshSoundList();
    refreshSettingsPage();
}

void MainWindow::openLibraryFolder(const QString& folder, bool remember)
{
    if (folder.isEmpty()) {
        return;
    }

    if (!m_library.open(folder)) {
        showEmptyPage(tr("Library unavailable"),
                      tr("Cuelet could not open the remembered library folder. Choose another folder or reconnect the drive."));
        QMessageBox::warning(this, tr("Cuelet"), m_library.lastError());
        return;
    }

    if (remember) {
        m_settings.setLibraryFolder(m_library.folder());
    }

    refreshCategories();
    refreshSoundList();
    refreshSettingsPage();
    showLibraryPage();
    updateActionStates();
    showWarningIfNeeded();
}

void MainWindow::chooseLibrary()
{
    const QString startFolder = m_library.hasLibrary() ? m_library.folder() : QDir::homePath();
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose Sound Library"), startFolder);
    openLibraryFolder(folder, true);
}

QStringList MainWindow::supportedAudioNameFilters() const
{
    LibraryScanner scanner;
    QStringList patterns;
    for (const QString& extension : scanner.supportedExtensions()) {
        patterns.append(QString("*.%1").arg(extension));
    }
    return {tr("Audio files (%1)").arg(patterns.join(' ')), tr("All files (*)")};
}

void MainWindow::importFiles()
{
    if (!m_library.hasLibrary()) {
        chooseLibrary();
        if (!m_library.hasLibrary()) {
            return;
        }
    }

    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Sounds"), QDir::homePath(), supportedAudioNameFilters().join(";;"));
    importLocalFiles(files);
}

void MainWindow::importLegacyConfig()
{
    const QString filter = tr("Legacy configs (soundboard.json settings.json config.json soundboard.conf);;JSON files (*.json);;Config files (*.conf);;All files (*)");
    const QString filePath = QFileDialog::getOpenFileName(this, tr("Import Legacy Soundboard Config"), QDir::homePath(), filter);
    if (filePath.isEmpty()) {
        return;
    }
    applyLegacyImportFile(filePath, false);
    QMessageBox::information(this, tr("Legacy Import"), m_settings.legacyMigrationSummary());
}

void MainWindow::importLocalFiles(const QStringList& files)
{
    if (files.isEmpty()) {
        return;
    }
    if (!m_library.importFiles(files)) {
        const QString message = !m_library.lastError().isEmpty() ? m_library.lastError() : m_library.lastWarning();
        QMessageBox::warning(this, tr("Import Sounds"), message);
        return;
    }

    refreshCategories();
    refreshSoundList();
    refreshSettingsPage();
    showLibraryPage();
    updateActionStates();
    showWarningIfNeeded();
}

void MainWindow::rescanLibrary()
{
    if (!m_library.rescan()) {
        QMessageBox::warning(this, tr("Rescan Library"), m_library.lastError());
        return;
    }

    refreshCategories();
    refreshSoundList();
    updateStatus();
    updateActionStates();
    showWarningIfNeeded();
}

void MainWindow::focusSearch()
{
    if (m_searchEdit) {
        showLibraryPage();
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    }
}

void MainWindow::playSelectedSound()
{
    const std::optional<SoundClip> clip = selectedClip();
    if (!clip.has_value()) {
        return;
    }
    if (clip->missing) {
        showTransientStatus(tr("This sound is missing from the library folder."), 6000);
        return;
    }
    if (!m_audio.play(clip->filePath)) {
        return;
    }
    m_playingRelativePath = clip->relativePath;
    m_playbackPulse = 12;
    if (m_playbackPulseTimer) {
        m_playbackPulseTimer->start();
    }
    refreshSoundList();
    updateActionStates();
}

void MainWindow::stopPlayback()
{
    m_audio.stopAll();
    m_playingRelativePath.clear();
    if (m_playbackPulseTimer) {
        m_playbackPulseTimer->stop();
    }
    refreshSoundList();
    showTransientStatus(tr("Playback stopped"), 2500);
    updateActionStates();
}

void MainWindow::editSelectedSound()
{
    const std::optional<SoundClip> clip = selectedClip();
    if (!clip.has_value()) {
        return;
    }

    MetadataDialog dialog(*clip, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (!m_library.updateMetadata(clip->relativePath, dialog.metadata())) {
        QMessageBox::warning(this, tr("Edit Sound"), m_library.lastError());
        return;
    }

    refreshCategories();
    refreshSoundList();
    updateActionStates();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this,
                       tr("About Cuelet"),
                       tr("<b>Cuelet</b><br>A native Qt soundboard for organizing, searching, and playing audio cues.<br><br>%1")
                           .arg(PlatformInfo::virtualMicrophoneNote()));
}

void MainWindow::handleSidebarChanged(int row)
{
    if (!m_library.hasLibrary()) {
        showEmptyPage();
        updateActionStates();
        return;
    }

    if (row == ProfilesRow) {
        showEmptyPage(tr("Profiles are coming next"),
                      tr("Profiles will let you keep separate soundboards for gaming, calls, streaming, school, and editing."));
        updateActionStates();
        return;
    }

    if (row == OverlayRow) {
        showEmptyPage(tr("Overlay mode is planned"),
                      tr("Cuelet will support a compact always-on-top mini soundboard with global hotkeys, opacity controls, and a permanent Stop All button."));
        updateActionStates();
        return;
    }

    showLibraryPage();
    if (row == CategoriesRow) {
        m_categoryCombo->showPopup();
    }
    refreshSoundList();
    updateActionStates();
}

QString MainWindow::currentCategoryFilter() const
{
    return m_categoryCombo ? m_categoryCombo->currentData().toString() : QString();
}

bool MainWindow::favoritesOnly() const
{
    return m_sidebar && m_sidebar->currentRow() == FavoritesRow;
}

QString MainWindow::displayTitleForClip(const SoundClip& clip) const
{
    if (!clip.metadata.title.trimmed().isEmpty()) {
        return clip.metadata.title.trimmed();
    }

    const QFileInfo info(clip.relativePath);
    return m_settings.showFileExtensions() ? info.fileName() : info.completeBaseName();
}

QString MainWindow::subtitleForClip(const SoundClip& clip) const
{
    QStringList parts;
    if (clip.missing) {
        parts << tr("Missing");
    }
    if (!clip.metadata.category.trimmed().isEmpty()) {
        parts << clip.metadata.category.trimmed();
    }
    parts << clip.relativePath;

    QString detail;
    if (!clip.metadata.notes.trimmed().isEmpty()) {
        detail = clip.metadata.notes.trimmed();
    } else if (!clip.metadata.aliases.isEmpty()) {
        detail = tr("Aliases: %1").arg(clip.metadata.aliases.mid(0, 3).join(", "));
    }
    if (!detail.isEmpty()) {
        parts << detail.left(72);
    }

    return parts.join(QStringLiteral(" - "));
}

QString MainWindow::emptySoundListMessage() const
{
    if (m_library.clips().isEmpty()) {
        return tr("No supported sounds were found in this library. Import audio files or choose another folder.");
    }
    if (!m_searchEdit->text().trimmed().isEmpty()) {
        return tr("No sounds match \"%1\". Try a different search or clear the category filter.").arg(m_searchEdit->text().trimmed());
    }
    if (favoritesOnly()) {
        return tr("No favorites yet. Select a sound and edit its metadata to mark it as a favorite.");
    }
    if (m_sidebar && m_sidebar->currentRow() == CategoriesRow) {
        return tr("No sounds match this category. Choose another category or clear the filter.");
    }
    return tr("No sounds match the current filters.");
}

void MainWindow::refreshCategories()
{
    const QString previous = currentCategoryFilter();
    m_categoryCombo->blockSignals(true);
    m_categoryCombo->clear();
    m_categoryCombo->addItem(tr("All categories"), QString());
    for (const QString& category : m_library.categories()) {
        m_categoryCombo->addItem(category, category);
    }
    const int previousIndex = m_categoryCombo->findData(previous);
    m_categoryCombo->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
    m_categoryCombo->blockSignals(false);

    if (m_sidebar) {
        const int favoriteCount = std::count_if(m_library.clips().cbegin(), m_library.clips().cend(), [](const SoundClip& clip) {
            return clip.metadata.favorite;
        });
        if (auto* item = m_sidebar->item(FavoritesRow)) {
            item->setText(favoriteCount > 0 ? tr("Favorites  %1").arg(favoriteCount) : tr("Favorites"));
        }
        if (auto* item = m_sidebar->item(CategoriesRow)) {
            const int categoryCount = m_library.categories().size();
            item->setText(categoryCount > 0 ? tr("Categories  %1").arg(categoryCount) : tr("Categories"));
        }
    }
}

void MainWindow::refreshSoundList()
{
    m_soundList->clear();

    if (!m_library.hasLibrary()) {
        updateStatus();
        return;
    }

    const QVector<SoundClip> clips = m_library.filteredClips(m_searchEdit->text(), favoritesOnly(), currentCategoryFilter());
    if (clips.isEmpty()) {
        const QString message = emptySoundListMessage();
        auto* item = new QListWidgetItem(message);
        item->setFlags(Qt::NoItemFlags);
        item->setTextAlignment(Qt::AlignCenter);
        item->setSizeHint(QSize(520, 88));
        m_soundList->addItem(item);
        updateStatus();
        return;
    }

    for (const SoundClip& clip : clips) {
        const bool playing = clip.relativePath == m_playingRelativePath;
        const QString title = displayTitleForClip(clip);
        const QString category = clip.metadata.category.trimmed().isEmpty() ? tr("Uncategorized") : clip.metadata.category.trimmed();
        auto* item = new QListWidgetItem(title);
        item->setData(RelativePathRole, clip.relativePath);
        item->setData(TitleRole, title);
        item->setData(CategoryRole, category);
        item->setData(ShortcutRole, tr("Key --"));
        item->setData(DurationRole, QString());
        item->setData(FavoriteRole, clip.metadata.favorite);
        item->setData(PlayingRole, playing);
        item->setData(MissingRole, clip.missing);
        item->setData(ProgressRole, playing ? m_playbackPulse : 0);
        item->setToolTip(clip.missing ? tr("File missing: %1").arg(clip.relativePath)
                                      : tr("%1\n%2\nDouble-click, Space, or Return to play.\nRight-click for edit, favorite, and reveal actions.")
                                            .arg(clip.filePath, subtitleForClip(clip)));
        item->setSizeHint(m_soundList->viewMode() == QListView::IconMode ? QSize(236, 154) : QSize(620, 86));
        if (clip.missing) {
            item->setForeground(QApplication::palette().color(QPalette::Disabled, QPalette::Text));
        }
        m_soundList->addItem(item);
    }

    updateStatus();
    updateActionStates();
}

void MainWindow::refreshAudioDevices()
{
    const QString previous = m_settings.audioOutputDeviceId();
    m_outputDeviceCombo->blockSignals(true);
    m_outputDeviceCombo->clear();
    m_outputDeviceCombo->addItem(tr("System default"), QString());
    for (const AudioOutputDevice& device : m_audio.outputDevices()) {
        const QString label = device.isDefault ? tr("%1 (default)").arg(device.description) : device.description;
        m_outputDeviceCombo->addItem(label, device.id);
    }
    const int index = m_outputDeviceCombo->findData(previous);
    if (index >= 0) {
        m_outputDeviceCombo->setCurrentIndex(index);
    } else {
        m_outputDeviceCombo->setCurrentIndex(0);
        m_audio.setOutputDeviceId(QString());
        m_settings.setAudioOutputDeviceId(QString());
    }
    m_outputDeviceCombo->blockSignals(false);
}

void MainWindow::refreshSettingsPage()
{
    m_settingsLibraryLabel->setText(m_library.hasLibrary() ? m_library.folder() : tr("No library selected"));
    m_settingsPathLabel->setText(tr("Settings file: %1").arg(m_settings.settingsFilePath()));

    m_volumeSlider->blockSignals(true);
    m_volumeSlider->setValue(qRound(m_settings.volume() * 100.0));
    m_volumeSlider->blockSignals(false);
    m_volumeValueLabel->setText(tr("%1%").arg(m_volumeSlider->value()));

    m_multiplePlaybackCheck->blockSignals(true);
    m_multiplePlaybackCheck->setChecked(m_settings.allowMultiplePlayback());
    m_multiplePlaybackCheck->blockSignals(false);

    m_showExtensionsCheck->blockSignals(true);
    m_showExtensionsCheck->setChecked(m_settings.showFileExtensions());
    m_showExtensionsCheck->blockSignals(false);

    m_loudnessCheck->blockSignals(true);
    m_loudnessCheck->setChecked(m_settings.loudnessNormalizationEnabled());
    m_loudnessCheck->blockSignals(false);
    m_loudnessStatusLabel->setText(m_settings.loudnessNormalizationEnabled()
                                       ? tr("Preference preserved, but loudness normalization is not active in the current Qt Multimedia backend yet.")
                                       : tr("Off. Cuelet stores this setting for compatibility; loudness processing is not implemented yet."));

    refreshAudioDevices();

    QStringList virtualMic;
    virtualMic << tr("Cuelet does not implement virtual microphone routing yet.");
    virtualMic << tr("Legacy virtual mic: %1").arg(boolStatus(m_settings.legacyVirtualMicEnabled()));
    virtualMic << tr("Legacy loopback: %1").arg(boolStatus(m_settings.legacyMicLoopbackEnabled()));
    if (!m_settings.legacyVirtualMicOutputDevice().isEmpty()) {
        virtualMic << tr("Legacy output device: %1").arg(m_settings.legacyVirtualMicOutputDevice());
    }
    if (!m_settings.legacyVirtualMicInputDevice().isEmpty()) {
        virtualMic << tr("Legacy input device: %1").arg(m_settings.legacyVirtualMicInputDevice());
    }
    virtualMic << tr("On macOS, future support would require a virtual device such as BlackHole plus CoreAudio routing.");
    m_virtualMicStatusLabel->setText(virtualMic.join('\n'));

    m_legacyStatusLabel->setText(m_settings.legacyMigrationAttempted()
                                     ? tr("Automatic migration has run.")
                                     : tr("Automatic migration has not run yet."));
    const QString source = m_settings.legacyMigrationSourcePath();
    const QString summary = m_settings.legacyMigrationSummary().trimmed();
    m_legacySummaryLabel->setText(summary.isEmpty()
                                      ? tr("No legacy import result has been recorded.")
                                      : (source.isEmpty() ? summary : tr("Source: %1\n%2").arg(source, summary)));
}

void MainWindow::showLibraryPage()
{
    if (!m_library.hasLibrary()) {
        showEmptyPage();
        return;
    }

    m_stack->setCurrentWidget(m_libraryPage);
    const QString folder = QDir::toNativeSeparators(m_library.folder());
    m_libraryPathLabel->setToolTip(folder);
    m_libraryPathLabel->setText(fontMetrics().elidedText(folder, Qt::ElideMiddle, 680));
}

void MainWindow::showEmptyPage(const QString& title, const QString& body)
{
    if (m_emptyTitleLabel) {
        m_emptyTitleLabel->setText(title.isEmpty() ? tr("No library selected yet") : title);
    }
    if (m_emptyBodyLabel) {
        m_emptyBodyLabel->setText(body.isEmpty()
                                      ? tr("Choose a folder of audio clips, or drag one here to build your soundboard.")
                                      : body);
    }
    if (m_emptyRecentLabel) {
        const QString remembered = m_settings.libraryFolder();
        m_emptyRecentLabel->setVisible(!remembered.isEmpty());
        m_emptyRecentLabel->setText(remembered.isEmpty() ? QString() : tr("Recent library: %1").arg(remembered));
    }
    m_stack->setCurrentWidget(m_emptyPage);
    updateActionStates();
}

void MainWindow::updateStatus()
{
    if (!m_library.hasLibrary()) {
        m_statusLabel->clear();
        if (m_nowPlayingStrip) {
            m_nowPlayingStrip->hide();
        }
        return;
    }

    const int total = m_library.clips().size();
    const int shown = m_library.filteredClips(m_searchEdit->text(), favoritesOnly(), currentCategoryFilter()).size();
    const int missing = std::count_if(m_library.clips().cbegin(), m_library.clips().cend(), [](const SoundClip& clip) {
        return clip.missing;
    });
    QString text = tr("%1 of %2 sounds shown").arg(shown).arg(total);
    if (missing > 0) {
        text += tr(" - %1 missing").arg(missing);
    }
    if (!m_library.unsupportedFiles().isEmpty()) {
        text += tr(" - %1 unsupported skipped").arg(m_library.unsupportedFiles().size());
    }
    m_statusLabel->setText(text);
    if (m_playingRelativePath.isEmpty()) {
        if (m_nowPlayingStrip) {
            m_nowPlayingStrip->hide();
        }
    } else {
        const QString nowPlaying = tr("Now playing: %1").arg(QFileInfo(m_playingRelativePath).completeBaseName());
        if (m_nowPlayingLabel && m_nowPlayingStrip) {
            m_nowPlayingLabel->setText(nowPlaying);
            m_nowPlayingStrip->show();
        }
    }
}

void MainWindow::updateActionStates()
{
    const bool hasLibrary = m_library.hasLibrary();
    const bool hasSelection = selectedClip().has_value();
    const bool isPlaying = !m_playingRelativePath.isEmpty();

    if (m_importAction) {
        m_importAction->setEnabled(true);
        m_importAction->setToolTip(hasLibrary ? tr("Import audio files into the current library")
                                              : tr("Choose a library folder, then import individual audio files"));
    }
    if (m_rescanAction) {
        m_rescanAction->setEnabled(hasLibrary);
    }
    if (m_playAction) {
        m_playAction->setEnabled(hasLibrary && hasSelection);
    }
    if (m_stopAction) {
        m_stopAction->setEnabled(isPlaying);
        m_stopAction->setVisible(isPlaying);
    }
    if (m_fadeOutAction) {
        m_fadeOutAction->setEnabled(false);
        m_fadeOutAction->setToolTip(tr("Fade-out is planned for a future audio engine pass"));
    }
    if (m_renameAction) {
        m_renameAction->setEnabled(hasLibrary && hasSelection);
    }
    if (m_editAction) {
        m_editAction->setEnabled(hasLibrary && hasSelection);
    }
    if (m_deleteAction) {
        m_deleteAction->setEnabled(false);
        m_deleteAction->setToolTip(hasLibrary && hasSelection ? tr("Delete with confirmation is planned")
                                                              : tr("Select a sound to delete"));
    }
    if (m_gridAction) {
        m_gridAction->setEnabled(hasLibrary);
    }
    if (m_listAction) {
        m_listAction->setEnabled(hasLibrary);
    }
    if (m_overlayAction) {
        m_overlayAction->setEnabled(false);
        m_overlayAction->setToolTip(tr("Compact overlay mode is planned"));
    }
}

void MainWindow::setSoundGridMode(bool gridMode)
{
    if (!m_soundList) {
        return;
    }

    m_soundList->setViewMode(gridMode ? QListView::IconMode : QListView::ListMode);
    m_soundList->setFlow(gridMode ? QListView::LeftToRight : QListView::TopToBottom);
    m_soundList->setWrapping(gridMode);
    m_soundList->setGridSize(gridMode ? QSize(246, 166) : QSize());
    m_soundList->setSpacing(gridMode ? 12 : 6);
    refreshSoundList();
}

void MainWindow::showTransientStatus(const QString& message, int timeoutMs)
{
    if (message.trimmed().isEmpty()) {
        return;
    }
    statusBar()->show();
    statusBar()->showMessage(message, timeoutMs);
    QTimer::singleShot(timeoutMs + 100, this, [this]() {
        if (statusBar()->currentMessage().isEmpty()) {
            statusBar()->hide();
        }
    });
}

void MainWindow::showWarningIfNeeded()
{
    if (!m_library.lastWarning().isEmpty()) {
        showTransientStatus(m_library.lastWarning(), 8000);
    }
}

std::optional<SoundClip> MainWindow::selectedClip() const
{
    const QListWidgetItem* item = m_soundList->currentItem();
    if (!item) {
        return std::nullopt;
    }

    const QString relativePath = item->data(RelativePathRole).toString();
    for (const SoundClip& clip : m_library.clips()) {
        if (clip.relativePath == relativePath) {
            return clip;
        }
    }
    return std::nullopt;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls()) {
        return;
    }

    for (const QUrl& url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QFileInfo info(url.toLocalFile());
        if (info.isDir() || LibraryScanner::isSupportedAudioFile(info.filePath())) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QStringList files;
    QStringList folders;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            const QFileInfo info(url.toLocalFile());
            if (info.isDir()) {
                folders.append(info.filePath());
            } else {
                files.append(info.filePath());
            }
        }
    }
    if (!folders.isEmpty() && !m_library.hasLibrary()) {
        openLibraryFolder(folders.first(), true);
        event->acceptProposedAction();
        return;
    }
    if (!files.isEmpty()) {
        importLocalFiles(files);
        event->acceptProposedAction();
        return;
    }
    if (!folders.isEmpty()) {
        showTransientStatus(tr("Drop audio files to import, or use Choose Library to switch folders."), 5000);
        event->acceptProposedAction();
    }
}
