#ifndef PS5_HOME_H
#define PS5_HOME_H

#include "common/trophies.h"

#include <QWidget>

#include <vector>

class MediaCard;
class QLabel;
class QLayout;
class QLineEdit;
class QListView;
class QPropertyAnimation;
class QPushButton;
class QToolButton;
class QTreeWidget;

// Lite home screen laid out like a console dashboard: a top row of game tiles (the selected one
// enlarged and captioned), the game's art behind it, its title and actions, and a row of wide
// cards. It shares the game list's model and selection, so ConfigurationListWidget keeps all the
// scanning, configuration and launch logic.
class Ps5Home: public QWidget {
	Q_OBJECT

public:
	explicit Ps5Home(QTreeWidget* list, QWidget* parent = nullptr);

	void SetRowHidden(int row, bool hidden);
	void FocusTiles();
	// Trophies earned in the game at `base_dir`; ignored once another game is selected.
	void SetGameTrophies(const QString& base_dir, const Common::Trophies::Progress& progress);
	// Trophies earned across `games` games.
	void SetAllTrophies(const Common::Trophies::Progress& progress, int games);

protected:
	// Console-style focus moves: arrows between and within the bar, tiles, actions and cards.
	bool eventFilter(QObject* watched, QEvent* event) override;

signals:
	void RunRequested();
	void SettingsRequested();
	void InputRequested();
	void TrophiesRequested();
	void TrophyOverviewRequested();
	void EditRequested();
	void OpenFolderRequested();
	void RefreshRequested();
	void SearchChanged(const QString& text);

private:
	void UpdateDetails();
	void EnsureSelection();
	[[nodiscard]] QString SelectedBaseDir() const;
	void                  FocusZone(int zone, int index);
	[[nodiscard]] bool    MoveFocus(QWidget* from, int key);
	void                  Activate(QWidget* widget);
	void                  SetControllerStatus(const QString& name, int battery);

	// Focus zones, top to bottom: bar buttons, store + tiles, actions, cards.
	std::vector<std::vector<QWidget*>> m_zones;
	std::vector<int>                   m_zone_index;
	QToolButton*                       m_more   = nullptr;

	QTreeWidget* m_list         = nullptr;
	QListView*   m_tiles        = nullptr;
	QLineEdit*   m_search       = nullptr;
	QLabel*      m_source       = nullptr;
	QLabel*      m_title        = nullptr;
	QLabel*      m_description  = nullptr;
	QLabel*      m_clock        = nullptr;
	QPushButton* m_play         = nullptr;
	MediaCard*   m_trophy_card     = nullptr;
	MediaCard*   m_all_trophy_card = nullptr;
	MediaCard*   m_pad_card        = nullptr;
	MediaCard*   m_storage_card    = nullptr;
	MediaCard*   m_setup_card      = nullptr;
	QWidget*     m_hero            = nullptr;
	// Game changes fade the hero text in, in step with the tile row's transition.
	QPropertyAnimation* m_hero_fade = nullptr;
	QString             m_art_base_dir;
};

// Drops the spacers and spacing left in layouts whose widgets the Lite home hides, so the hidden
// toolbar and settings strip take no room.
void CollapseLayout(QLayout* layout);

#endif // PS5_HOME_H
