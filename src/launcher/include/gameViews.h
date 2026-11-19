#ifndef GAME_VIEWS_H
#define GAME_VIEWS_H

#include "gameContent.h"

#include <QCache>
#include <QGuiApplication>
#include <QIcon>
#include <QListView>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QString>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVariantAnimation>

#include <algorithm>

inline constexpr int GAME_NAME_COLUMN             = 0;
inline constexpr int GAME_SERIAL_COLUMN           = 1;
inline constexpr int GAME_VERSION_COLUMN          = 2;
inline constexpr int GAME_FIRMWARE_VERSION_COLUMN = 3;
inline constexpr int GAME_SIZE_COLUMN             = 4;
inline constexpr int GAME_PATH_COLUMN             = 5;
inline constexpr int GAME_STATUS_COLUMN           = 6;
inline constexpr int GAME_COMMENT_COLUMN          = 7;

// Holds the transparent list and grid views and paints the selected game's pic0 behind both.
class GameViews: public QStackedWidget {
public:
	explicit GameViews(QWidget* parent = nullptr): QStackedWidget(parent) {
		m_cache.setMaxCost(48 * 1024); // KiB: a handful of full-size pic0 images
		// The art holds for 180 ms while the game tiles resize, then eases over 300 ms.
		m_fade.setDuration(480);
		m_fade.setStartValue(0.0);
		m_fade.setEndValue(1.0);
		connect(&m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
			const qreal t = std::clamp((value.toReal() * 480.0 - 180.0) / 300.0, 0.0, 1.0);
			m_fade_value  = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
			update();
		});
		connect(&m_fade, &QVariantAnimation::finished, this, [this]() {
			m_fade_value = 1.0;
			m_previous   = QPixmap();
			update();
		});
	}

	void SetBackgroundImage(const QString& key) {
		if (m_path == key) {
			return;
		}
		m_path = key;
		// Lite: games revisited while browsing skip the archive read and PNG decode.
		if (const auto* cached = m_cache.object(key); cached != nullptr) {
			m_source = *cached;
		} else {
			m_source = QPixmap();
			if (!key.isEmpty()) {
				m_source.loadFromData(GameContent::ReadFile(key, QStringLiteral("sce_sys/pic0.png"),
				                                            GameContent::MaxImageSize));
				m_cache.insert(key, new QPixmap(m_source),
				               m_source.width() * m_source.height() * 4 / 1024 + 1);
			}
		}
		// Lite: cross-fade from the art on screen, the way a console dashboard switches games.
		m_previous = isVisible() ? m_scaled : QPixmap();
		UpdateScaledBackground();
		if (m_previous.isNull()) {
			m_fade.stop();
			m_fade_value = 1.0;
		} else {
			m_fade.stop();
			m_fade.start();
		}
		update();
	}

protected:
	void resizeEvent(QResizeEvent* event) override {
		QStackedWidget::resizeEvent(event);
		UpdateScaledBackground();
	}

	void paintEvent(QPaintEvent* event) override {
		QPainter   painter(this);
		const auto layer = [&](const QPixmap& art) {
			if (art.isNull()) {
				QLinearGradient blue(rect().topLeft(), rect().bottomRight());
				blue.setColorAt(0, QColor(12, 34, 82));
				blue.setColorAt(1, QColor(2, 8, 24));
				painter.fillRect(event->rect(), blue);
			} else {
				painter.drawPixmap(0, 0, art); // clipped to the update region
			}
		};
		// Two blits of pre-shaded, widget-sized frames, so a fade costs little per frame.
		if (m_fade_value < 1.0) {
			layer(m_previous);
			painter.setOpacity(m_fade_value);
		}
		layer(m_scaled);
	}

private:
	// The art cover-scaled to the widget with the shading baked in.
	void UpdateScaledBackground() {
		if (m_source.isNull() || size().isEmpty()) {
			m_scaled = QPixmap();
			return;
		}
		const qreal ratio = devicePixelRatioF();
		auto        art   = m_source.scaled(size() * ratio, Qt::KeepAspectRatioByExpanding,
		                                    Qt::SmoothTransformation);
		art.setDevicePixelRatio(ratio);
		m_scaled = QPixmap(size() * ratio);
		m_scaled.setDevicePixelRatio(ratio);
		QPainter    painter(&m_scaled);
		const QRect area = rect();
		const auto logical = art.deviceIndependentSize();
		painter.drawPixmap(QPointF((width() - logical.width()) / 2, (height() - logical.height()) / 2),
		                   art);
		// Lite: console-style shading keeps text readable on the left and bottom, art on the right.
		QLinearGradient left(area.topLeft(), area.topRight());
		left.setColorAt(0, QColor(0, 0, 0, 215));
		left.setColorAt(0.55, QColor(0, 0, 0, 90));
		left.setColorAt(1, QColor(0, 0, 0, 0));
		painter.fillRect(area, left);
		QLinearGradient bottom(area.bottomLeft(), area.topLeft());
		bottom.setColorAt(0, QColor(0, 0, 0, 225));
		bottom.setColorAt(0.45, QColor(0, 0, 0, 40));
		bottom.setColorAt(1, QColor(0, 0, 0, 120));
		painter.fillRect(area, bottom);
	}

	QString                  m_path;
	QPixmap                  m_source;
	QPixmap                  m_scaled;
	QPixmap                  m_previous; // the art fading out
	QVariantAnimation        m_fade;
	qreal                    m_fade_value = 1.0;
	QCache<QString, QPixmap> m_cache;
};

// Marks a title that supports PS5 Pro mode with a pill in the top-right corner of its cover.
inline void PaintProBadge(QPainter* painter, const QRect& cover, QFont font, const QString& text) {
	font.setBold(true);
	const QFontMetrics metrics(font);
	const int          inset = metrics.height() / 3;
	QRect              badge(0, 0, metrics.horizontalAdvance(text) + 2 * inset, metrics.height());
	badge.moveTopRight(cover.topRight() + QPoint(-inset, inset));
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	painter->setPen(Qt::NoPen);
	painter->setBrush(QColor(0x534ab7));
	painter->drawRoundedRect(badge, 4, 4);
	painter->setFont(font);
	painter->setPen(QColor(0xeeedfe));
	painter->drawText(badge, Qt::AlignCenter, text);
	painter->restore();
}

// A card on a translucent panel, so the text keeps its contrast over the game background.
class GameGridDelegate: public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	QSize sizeHint(const QStyleOptionViewItem& option,
	               const QModelIndex& /*index*/) const override {
		return option.decorationSize +
		       QSize(2 * CARD_PADDING, 3 * CARD_PADDING + 2 * option.fontMetrics.lineSpacing());
	}

	void paint(QPainter* painter, const QStyleOptionViewItem& option,
	           const QModelIndex& index) const override {
		// The stylesheet made the view's own palette transparent, so take the panel from the app's.
		auto panel      = QGuiApplication::palette().color(QPalette::Window);
		auto text_color = option.palette.color(QPalette::Text);
		// Nearly opaque: the console-style shading behind darkens a lighter panel past readability.
		panel.setAlpha(235);
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		painter->setPen(option.state.testFlag(QStyle::State_Selected) ? QPen(text_color)
		                                                              : Qt::NoPen);
		painter->setBrush(panel);
		painter->drawRoundedRect(QRectF(option.rect).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

		const QRect cover(option.rect.topLeft() + QPoint(CARD_PADDING, CARD_PADDING),
		                  option.decorationSize);
		index.data(Qt::DecorationRole).value<QIcon>().paint(painter, cover);
		if (index.data(Qt::UserRole).toBool()) {
			PaintProBadge(painter, cover, option.font, QStringLiteral("PS5 PRO"));
		}

		QRect line(cover.left(), cover.bottom() + 1 + CARD_PADDING, cover.width(),
		           option.fontMetrics.lineSpacing());
		auto  title_font = option.font;
		title_font.setBold(true);
		painter->setFont(title_font);
		painter->setPen(text_color);
		painter->drawText(line, Qt::AlignLeft | Qt::AlignVCenter,
		                  painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight,
		                                                    line.width()));

		line.translate(0, line.height());
		const auto size    = index.siblingAtColumn(GAME_SIZE_COLUMN).data().toString();
		const auto details = QStringLiteral("%1  %2").arg(
		    index.siblingAtColumn(GAME_SERIAL_COLUMN).data().toString(),
		    index.siblingAtColumn(GAME_VERSION_COLUMN).data().toString());
		text_color.setAlpha(200);
		painter->setFont(option.font);
		painter->setPen(text_color);
		painter->drawText(line, Qt::AlignRight | Qt::AlignVCenter, size);
		line.setRight(line.right() - option.fontMetrics.horizontalAdvance(size) - CARD_PADDING);
		painter->drawText(line, Qt::AlignLeft | Qt::AlignVCenter,
		                  option.fontMetrics.elidedText(details, Qt::ElideRight, line.width()));
		painter->restore();
	}

private:
	static constexpr int CARD_PADDING = 8;
};

// The list's name cell, with the PS5 Pro badge over its icon.
class GameListNameDelegate: public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void paint(QPainter* painter, const QStyleOptionViewItem& option,
	           const QModelIndex& index) const override {
		QStyledItemDelegate::paint(painter, option, index);
		if (!index.data(Qt::UserRole).toBool()) {
			return;
		}
		QStyleOptionViewItem opt(option);
		initStyleOption(&opt, index);
		const auto icon = opt.widget->style()->subElementRect(QStyle::SE_ItemViewItemDecoration,
		                                                      &opt, opt.widget);
		// The list's small icon takes a smaller badge than the grid's covers.
		auto font = opt.font;
		font.setPixelSize(icon.height() / 5);
		PaintProBadge(painter, icon, font, QStringLiteral("PRO"));
	}
};

// Shows the list's games as cards in the grid: both views share the items and the current game.
inline void SetupGameGrid(QTreeWidget* list, QListView* grid) {
	grid->setModel(list->model());
	grid->setSelectionModel(list->selectionModel());
	grid->setViewMode(QListView::IconMode);
	grid->setMovement(QListView::Static);
	grid->setResizeMode(QListView::Adjust);
	grid->setSpacing(8);
	grid->setUniformItemSizes(true);
	grid->setItemDelegate(new GameGridDelegate(grid));
	// A card also shows other columns (the size arrives late), which the grid does not repaint on.
	QObject::connect(list->model(), &QAbstractItemModel::dataChanged, grid->viewport(),
	                 qOverload<>(&QWidget::update));
}

#endif // GAME_VIEWS_H
