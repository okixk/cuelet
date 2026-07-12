#pragma once

#include "app/SoundLibrary.h"
#include "audio/QtAudioService.h"
#include "storage/AppSettings.h"

#include <QMainWindow>

#include <optional>

class QCheckBox;
class QComboBox;
class QAction;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSlider;
class QSplitter;
class QStackedWidget;
class QFrame;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void chooseLibrary();
    void importFiles();
    void rescanLibrary();
    void focusSearch();
    void playSelectedSound();
    void stopPlayback();
    void editSelectedSound();
    void showAbout();
    void handleSidebarChanged(int row);
    void refreshSoundList();
    void refreshSettingsPage();
    void refreshAudioDevices();
    void importLegacyConfig();

private:
    enum SidebarRow {
        LibraryRow = 0,
        FavoritesRow = 1,
        RecentRow = 2,
        CategoriesRow = 3,
        ProfilesRow = 4,
        OverlayRow = 5,
    };

    void setupUi();
    void setupActions();
    void applyStyle();
    void loadRememberedLibrary();
    void attemptAutomaticLegacyImport();
    void openLibraryFolder(const QString& folder, bool remember);
    void importLocalFiles(const QStringList& files);
    void applyLegacyImportFile(const QString& filePath, bool automatic);
    void refreshCategories();
    void showLibraryPage();
    void showEmptyPage(const QString& title = QString(), const QString& body = QString());
    void updateStatus();
    void updateActionStates();
    void setSoundGridMode(bool gridMode);
    void showTransientStatus(const QString& message, int timeoutMs = 3000);
    void showWarningIfNeeded();
    QString currentCategoryFilter() const;
    bool favoritesOnly() const;
    std::optional<SoundClip> selectedClip() const;
    QString displayTitleForClip(const SoundClip& clip) const;
    QString subtitleForClip(const SoundClip& clip) const;
    QString emptySoundListMessage() const;
    QStringList supportedAudioNameFilters() const;

    SoundLibrary m_library;
    AppSettings m_settings;
    QtAudioService m_audio;

    QStackedWidget* m_stack = nullptr;
    QSplitter* m_splitter = nullptr;
    QWidget* m_sidebarPanel = nullptr;
    QWidget* m_emptyPage = nullptr;
    QWidget* m_libraryPage = nullptr;
    QWidget* m_settingsPage = nullptr;
    QListWidget* m_sidebar = nullptr;
    QPushButton* m_sidebarSettingsButton = nullptr;
    QLabel* m_emptyTitleLabel = nullptr;
    QLabel* m_emptyBodyLabel = nullptr;
    QLabel* m_emptyRecentLabel = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QComboBox* m_categoryCombo = nullptr;
    QListWidget* m_soundList = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_libraryPathLabel = nullptr;
    QFrame* m_nowPlayingStrip = nullptr;
    QLabel* m_nowPlayingLabel = nullptr;
    QPushButton* m_nowPlayingStopButton = nullptr;
    QLabel* m_settingsLibraryLabel = nullptr;
    QLabel* m_settingsPathLabel = nullptr;
    QLabel* m_volumeValueLabel = nullptr;
    QLabel* m_loudnessStatusLabel = nullptr;
    QLabel* m_virtualMicStatusLabel = nullptr;
    QLabel* m_legacyStatusLabel = nullptr;
    QLabel* m_legacySummaryLabel = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QComboBox* m_outputDeviceCombo = nullptr;
    QCheckBox* m_multiplePlaybackCheck = nullptr;
    QCheckBox* m_showExtensionsCheck = nullptr;
    QCheckBox* m_loudnessCheck = nullptr;
    QAction* m_importAction = nullptr;
    QAction* m_rescanAction = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_fadeOutAction = nullptr;
    QAction* m_renameAction = nullptr;
    QAction* m_editAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_gridAction = nullptr;
    QAction* m_listAction = nullptr;
    QAction* m_overlayAction = nullptr;
    QString m_playingRelativePath;
    QTimer* m_playbackPulseTimer = nullptr;
    int m_playbackPulse = 0;
};
