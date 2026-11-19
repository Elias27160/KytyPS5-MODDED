#ifndef TROPHY_VIEWER_DIALOG_H
#define TROPHY_VIEWER_DIALOG_H

#include "common/trophies.h"

#include <QDialog>

#include <vector>

class QTabWidget;
class QWidget;

class Configuration;
class TrophyViewerDialog: public QDialog {
public:
	explicit TrophyViewerDialog(QWidget* parent = nullptr);

	static bool HasTrophyData(const Configuration* info);
	// Earned and total trophies of a game, read from its package and the saved unlocks. Reads
	// files only, so it can run off the GUI thread.
	static Common::Trophies::Progress GetProgress(const Configuration* info,
	                                              const QString&       runtime_directory);
	static void ShowForGame(const Configuration* info, const QString& runtime_directory,
	                        QWidget* parent);
	static void ShowOverview(const std::vector<const Configuration*>& games,
	                         const QString& runtime_directory, QWidget* parent);

private:
	bool LoadGame(const Configuration& info, const QString& runtime_directory, QString& error);

	QTabWidget* m_tabs = nullptr;
};

#endif // TROPHY_VIEWER_DIALOG_H
