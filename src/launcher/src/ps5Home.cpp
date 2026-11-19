#include "ps5Home.h"

#include "configuration.h"
#include "configurationItem.h"
#include "gameContent.h"
#include "gameViews.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QStorageInfo>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace {

constexpr int TILE_SMALL   = 80;
constexpr int TILE_LARGE   = 120;
constexpr int TILE_SPACING = 10;
constexpr int TILE_RADIUS  = 10;

const char* const STORE_URL = "https://kytyps5.github.io/";

// The selected game's name sits to the right of its tile, under the smaller neighbours.
//
// A selection change plays like the console row: the new tile grows while the old one shrinks and
// the row glides so the selection keeps its slot (one tile in from the left), then the caption
// fades in. All of it is one 450 ms animation; the background and hero text follow its timing.
class TileRow: public QListView {
public:
	static constexpr int TransitionMs = 450;

	explicit TileRow(QWidget* parent): QListView(parent) {
		m_animation.setDuration(TransitionMs);
		m_animation.setStartValue(0.0);
		m_animation.setEndValue(1.0);
		connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
			m_ms = value.toReal() * TransitionMs;
			Step();
		});
	}

	// Plays the move from `previous` to the current row.
	void Transition(int previous) {
		m_previous = previous;
		m_from     = horizontalScrollBar()->value();
		if (!isVisible()) {
			m_animation.stop();
			m_ms = TransitionMs;
			Step();
			return;
		}
		m_animation.stop();
		m_ms = 0;
		m_animation.start();
	}

	// Tile edge at this point of the transition.
	[[nodiscard]] int TileSize(int row) const {
		const qreal grow = Grow();
		const int   current = currentIndex().row();
		if (row == current) {
			return qRound(TILE_SMALL + (TILE_LARGE - TILE_SMALL) * grow);
		}
		if (row == m_previous && grow < 1.0) {
			return qRound(TILE_LARGE - (TILE_LARGE - TILE_SMALL) * grow);
		}
		return TILE_SMALL;
	}

protected:
	void paintEvent(QPaintEvent* event) override {
		QListView::paintEvent(event);
		const auto current = currentIndex();
		// The caption waits for the tile to settle, then fades in.
		const qreal alpha = std::clamp((m_ms - 170.0) / 180.0, 0.0, 1.0);
		if (!current.isValid() || isRowHidden(current.row()) || alpha <= 0.0) {
			return;
		}
		const QRect tile = visualRect(current);
		QPainter    painter(viewport());
		auto        font = this->font();
		font.setPixelSize(18);
		painter.setFont(font);
		painter.setOpacity(alpha);
		painter.setPen(Qt::white);
		const QRect caption(tile.left() + TILE_LARGE + 14, tile.top() + TILE_SMALL + 10,
		                    viewport()->width() - tile.left() - TILE_LARGE - 20, 28);
		painter.drawText(caption, Qt::AlignLeft | Qt::AlignVCenter,
		                 QFontMetrics(font).elidedText(current.data().toString(), Qt::ElideRight,
		                                               caption.width()));
	}

	void focusInEvent(QFocusEvent* event) override {
		QListView::focusInEvent(event);
		viewport()->update();
	}

	void focusOutEvent(QFocusEvent* event) override {
		QListView::focusOutEvent(event);
		viewport()->update();
	}

private:
	// The size change takes the first 160 ms, easing out.
	[[nodiscard]] qreal Grow() const {
		const qreal t = std::clamp(m_ms / 160.0, 0.0, 1.0);
		return 1.0 - std::pow(1.0 - t, 3.0);
	}

	void Step() {
		doItemsLayout();
		// Settled layout: every visible tile before the selection is small; keep one of them in view.
		const int current = currentIndex().row();
		int       before  = 0;
		for (int row = 0; row < current; row++) {
			before += isRowHidden(row) ? 0 : 1;
		}
		const int to = std::clamp((before - 1) * (TILE_SMALL + TILE_SPACING), 0,
		                          std::max(horizontalScrollBar()->maximum(), 0));
		horizontalScrollBar()->setValue(qRound(m_from + (to - m_from) * Grow()));
		viewport()->update();
	}

	QVariantAnimation m_animation;
	qreal             m_ms       = TransitionMs;
	int               m_previous = -1;
	int               m_from     = 0;
};

// Tiles are top-aligned like the console row: small ones, and a larger outlined selected one.
class TileDelegate: public QStyledItemDelegate {
public:
	explicit TileDelegate(TileRow* view): QStyledItemDelegate(view), m_view(view) {}

	[[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& /*option*/,
	                             const QModelIndex& index) const override {
		return {m_view->TileSize(index.row()) + TILE_SPACING, TILE_LARGE};
	}

	void paint(QPainter* painter, const QStyleOptionViewItem& option,
	           const QModelIndex& index) const override {
		const bool  current = IsCurrent(index);
		const int   size    = m_view->TileSize(index.row());
		const QRect tile(option.rect.x(), option.rect.y(), size, size);

		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		painter->setRenderHint(QPainter::SmoothPixmapTransform);
		QPainterPath path;
		path.addRoundedRect(QRectF(tile), TILE_RADIUS, TILE_RADIUS);
		const auto icon = index.data(Qt::DecorationRole).value<QIcon>();
		if (icon.isNull()) {
			painter->fillPath(path, QColor(28, 36, 56));
			painter->setPen(Qt::white);
			painter->drawText(tile.adjusted(6, 6, -6, -6), Qt::AlignCenter | Qt::TextWordWrap,
			                  index.data().toString());
		} else {
			painter->setClipPath(path);
			painter->drawPixmap(tile,
			                    icon.pixmap(tile.size(), painter->device()->devicePixelRatioF()));
			painter->setClipping(false);
		}
		if (current && m_view->hasFocus()) {
			painter->setPen(QPen(QColor(255, 255, 255, 240), 3));
			painter->drawPath(path);
		}
		painter->restore();
	}

private:
	[[nodiscard]] bool IsCurrent(const QModelIndex& index) const {
		return index.row() == m_view->currentIndex().row();
	}

	TileRow* m_view;
};

// Draws a white line icon into a square pixmap.
QIcon PaintedIcon(int size, qreal ratio, const std::function<void(QPainter&)>& draw) {
	QPixmap pixmap(QSize(size, size) * ratio);
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	draw(painter);
	return QIcon(pixmap);
}

QIcon SearchIcon(qreal ratio) {
	return PaintedIcon(30, ratio, [](QPainter& p) {
		p.drawEllipse(QRectF(5, 5, 15, 15));
		p.drawLine(QPointF(17.5, 17.5), QPointF(25, 25));
	});
}

QIcon GearIcon(qreal ratio) {
	return PaintedIcon(30, ratio, [](QPainter& p) {
		QPainterPath teeth;
		const QPointF center(15, 15);
		for (int i = 0; i < 8; i++) {
			QTransform t;
			t.translate(center.x(), center.y());
			t.rotate(i * 45.0);
			teeth.addPolygon(t.map(QPolygonF(QRectF(-2.6, -12.5, 5.2, 5))));
		}
		teeth.addEllipse(center, 8.5, 8.5);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawPath(teeth.simplified());
		p.setBrush(QColor(0, 0, 0, 0));
		p.setCompositionMode(QPainter::CompositionMode_Clear);
		p.drawEllipse(center, 3.6, 3.6);
	});
}

// Store tile artwork: a blue shopping bag with a star, not any console maker's mark.
QIcon StoreIcon(qreal ratio) {
	return PaintedIcon(44, ratio, [](QPainter& p) {
		QLinearGradient body(0, 12, 0, 40);
		body.setColorAt(0, QColor(64, 140, 255));
		body.setColorAt(1, QColor(20, 70, 200));
		p.setPen(QPen(Qt::white, 2.6, Qt::SolidLine, Qt::RoundCap));
		p.drawArc(QRectF(14, 4, 16, 16), 0, 180 * 16);
		p.setPen(Qt::NoPen);
		p.setBrush(body);
		p.drawRoundedRect(QRectF(7, 12, 30, 28), 5, 5);
		QPolygonF star;
		for (int i = 0; i < 10; i++) {
			const double radius = (i % 2 == 0) ? 8.0 : 3.5;
			const double angle  = (i * 36.0 - 90.0) * 3.14159265358979 / 180.0;
			star << QPointF(22 + radius * std::cos(angle), 26 + radius * std::sin(angle));
		}
		p.setBrush(Qt::white);
		p.drawPolygon(star);
	});
}

QIcon TintedIcon(const QString& resource, int size, qreal ratio) {
	auto     pixmap = QIcon(resource).pixmap(QSize(size, size), ratio);
	QPainter painter(&pixmap);
	painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
	painter.fillRect(pixmap.rect(), Qt::white);
	painter.end();
	return QIcon(pixmap);
}

QFont PixelFont(const QFont& base, int pixels, QFont::Weight weight) {
	auto font = base;
	font.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Noto Sans")});
	font.setPixelSize(pixels);
	font.setWeight(weight);
	return font;
}

// Drives the launcher with a game controller through the keys the dashboard and every dialog
// already understand, like a console menu: d-pad or left stick move, Cross/A accepts, Circle/B
// goes back, L1/R1 step between fields, Triangle/Y opens the options menu (Key_Menu) and
// Options/Start the settings (F2). Controllers are read only while a launcher window is active and
// released otherwise, so a running game owns them.
class GamepadNavigator: public QObject {
public:
	// Name and battery (-1 unknown, 101 wired) of the first controller; an empty name for none.
	using StatusCallback = std::function<void(const QString& name, int battery)>;

	GamepadNavigator(QObject* parent, StatusCallback on_status)
	    : QObject(parent), m_on_status(std::move(on_status)) {
		m_clock.start();
		connect(&m_timer, &QTimer::timeout, this, [this]() { Poll(); });
		m_timer.start(16);
	}

	~GamepadNavigator() override { Release(); }

private:
	struct Repeat {
		bool   down = false;
		qint64 next = 0;
	};

	static constexpr qint64 RepeatDelayMs = 350;
	static constexpr qint64 RepeatRateMs  = 110;
	static constexpr float  StickPress    = 0.5f;

	void Poll() {
		if (QApplication::activeWindow() == nullptr) {
			Release();
			return;
		}
		if (!m_initialized) {
			SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
			if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
				return;
			}
			// State is read directly; nobody drains per-input events, so do not queue them.
			for (const auto type:
			     {SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_JOYSTICK_BALL_MOTION,
			      SDL_EVENT_JOYSTICK_HAT_MOTION, SDL_EVENT_JOYSTICK_BUTTON_DOWN,
			      SDL_EVENT_JOYSTICK_BUTTON_UP, SDL_EVENT_JOYSTICK_UPDATE_COMPLETE,
			      SDL_EVENT_GAMEPAD_AXIS_MOTION, SDL_EVENT_GAMEPAD_BUTTON_DOWN,
			      SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN,
			      SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION, SDL_EVENT_GAMEPAD_TOUCHPAD_UP,
			      SDL_EVENT_GAMEPAD_SENSOR_UPDATE, SDL_EVENT_GAMEPAD_UPDATE_COMPLETE}) {
				SDL_SetEventEnabled(type, false);
			}
			m_initialized = true;
			m_scan_ticks  = 0;
		}
		if (m_scan_ticks-- <= 0) {
			OpenGamepads();
			m_scan_ticks = 60; // look for newly connected controllers about once a second
		}
		SDL_UpdateGamepads();

		bool up = false, down = false, left = false, right = false;
		bool south = false, east = false, north = false, start = false, l1 = false, r1 = false;
		for (auto* gamepad: m_gamepads) {
			const auto button = [gamepad](SDL_GamepadButton b) { return SDL_GetGamepadButton(gamepad, b); };
			const auto axis   = [gamepad](SDL_GamepadAxis a) {
                return static_cast<float>(SDL_GetGamepadAxis(gamepad, a)) / 32767.0f;
			};
			up |= button(SDL_GAMEPAD_BUTTON_DPAD_UP) || axis(SDL_GAMEPAD_AXIS_LEFTY) < -StickPress;
			down |= button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || axis(SDL_GAMEPAD_AXIS_LEFTY) > StickPress;
			left |= button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || axis(SDL_GAMEPAD_AXIS_LEFTX) < -StickPress;
			right |= button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || axis(SDL_GAMEPAD_AXIS_LEFTX) > StickPress;
			south |= button(SDL_GAMEPAD_BUTTON_SOUTH);
			east |= button(SDL_GAMEPAD_BUTTON_EAST);
			north |= button(SDL_GAMEPAD_BUTTON_NORTH);
			start |= button(SDL_GAMEPAD_BUTTON_START);
			l1 |= button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
			r1 |= button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
		}
		const auto now = m_clock.elapsed();
		Held(m_up, up, now, Qt::Key_Up);
		Held(m_down, down, now, Qt::Key_Down);
		Held(m_left, left, now, Qt::Key_Left);
		Held(m_right, right, now, Qt::Key_Right);
		Pressed(m_south, south, AcceptKey());
		Pressed(m_east, east, Qt::Key_Escape);
		Pressed(m_north, north, Qt::Key_Menu);
		Pressed(m_start, start, Qt::Key_F2);
		Pressed(m_l1, l1, Qt::Key_Backtab);
		Pressed(m_r1, r1, Qt::Key_Tab);
	}

	void OpenGamepads() {
		int   count = 0;
		auto* ids   = SDL_GetGamepads(&count);
		for (int i = 0; i < count; i++) {
			if (!m_gamepads.contains(ids[i])) {
				if (auto* gamepad = SDL_OpenGamepad(ids[i]); gamepad != nullptr) {
					m_gamepads.insert(ids[i], gamepad);
				}
			}
		}
		SDL_free(ids);
		for (auto it = m_gamepads.begin(); it != m_gamepads.end();) {
			if (!SDL_GamepadConnected(it.value())) {
				SDL_CloseGamepad(it.value());
				it = m_gamepads.erase(it);
			} else {
				++it;
			}
		}
		QString name;
		int     battery = -1;
		if (!m_gamepads.isEmpty()) {
			auto*      gamepad = m_gamepads.begin().value();
			const char* text   = SDL_GetGamepadName(gamepad);
			name               = text != nullptr ? QString::fromUtf8(text) : tr("Manette");
			int        percent = -1;
			const auto state   = SDL_GetGamepadPowerInfo(gamepad, &percent);
			battery            = state == SDL_POWERSTATE_NO_BATTERY ? 101 : percent;
		}
		if (m_on_status && (name != m_status_name || battery != m_status_battery)) {
			m_status_name    = name;
			m_status_battery = battery;
			m_on_status(name, battery);
		}
	}

	void Release() {
		if (!m_initialized) {
			return;
		}
		for (auto* gamepad: m_gamepads) {
			SDL_CloseGamepad(gamepad);
		}
		m_gamepads.clear();
		SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
		m_initialized = false;
		m_up = m_down = m_left = m_right = {};
		m_south = m_east = m_north = m_start = m_l1 = m_r1 = false;
	}

	// Buttons and combo boxes take Space; lists, menus, fields and dialogs take Return.
	static int AcceptKey() {
		auto* focus = QApplication::focusWidget();
		return QApplication::activePopupWidget() == nullptr &&
		               (qobject_cast<QAbstractButton*>(focus) != nullptr ||
		                qobject_cast<QComboBox*>(focus) != nullptr)
		           ? Qt::Key_Space
		           : Qt::Key_Return;
	}

	static void SendKey(int key) {
		QWidget* target = QApplication::activePopupWidget();
		if (target == nullptr) target = QApplication::focusWidget();
		if (target == nullptr) target = QApplication::activeWindow();
		if (target == nullptr) return;
		QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
		QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
		QCoreApplication::sendEvent(target, &press);
		QCoreApplication::sendEvent(target, &release);
	}

	static void Pressed(bool& previous, bool down, int key) {
		if (down && !previous) {
			SendKey(key);
		}
		previous = down;
	}

	static void Held(Repeat& state, bool down, qint64 now, int key) {
		if (!down) {
			state.down = false;
			return;
		}
		if (!state.down) {
			state = {true, now + RepeatDelayMs};
			SendKey(key);
		} else if (now >= state.next) {
			state.next = now + RepeatRateMs;
			SendKey(key);
		}
	}

	QTimer                              m_timer;
	QElapsedTimer                       m_clock;
	QHash<SDL_JoystickID, SDL_Gamepad*> m_gamepads;
	bool                                m_initialized = false;
	int                                 m_scan_ticks  = 0;
	StatusCallback                      m_on_status;
	QString                             m_status_name;
	int                                 m_status_battery = -2;
	Repeat                              m_up, m_down, m_left, m_right;
	bool m_south = false, m_east = false, m_north = false, m_start = false, m_l1 = false, m_r1 = false;
};

} // namespace

// Wide media card: cover-cropped picture (or gradient), dark lower shade and a caption.
class MediaCard: public QWidget {
public:
	MediaCard(QWidget* parent, QColor top, QColor bottom, std::function<void()> on_click)
	    : QWidget(parent), m_top(top), m_bottom(bottom), m_on_click(std::move(on_click)) {
		setCursor(Qt::PointingHandCursor);
		setMinimumWidth(160);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setFixedHeight(116);
		setAttribute(Qt::WA_Hover);
		setFocusPolicy(Qt::StrongFocus);
	}

	void Click() {
		if (m_on_click) {
			m_on_click();
		}
	}

	void SetContent(const QString& caption, const QString& badge, const QPixmap& picture) {
		m_caption = caption;
		m_badge   = badge;
		m_picture = picture;
		m_scaled  = QPixmap();
		update();
	}

	void SetIcon(const QIcon& icon) {
		m_icon = icon;
		update();
	}

	// A large value under the badge and, with a progress in [0, 1], a bar above the caption.
	void SetStats(const QString& value, qreal progress = -1.0) {
		m_value    = value;
		m_progress = progress;
		update();
	}

protected:
	void paintEvent(QPaintEvent* /*event*/) override {
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		const QRectF area = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
		QPainterPath path;
		path.addRoundedRect(area, 8, 8);
		painter.setClipPath(path);
		if (m_picture.isNull()) {
			QLinearGradient fill(area.topLeft(), area.bottomRight());
			fill.setColorAt(0, m_top);
			fill.setColorAt(1, m_bottom);
			painter.fillRect(area, fill);
		} else {
			// Scaled once per picture and size: the background fade repaints the cards every frame.
			if (m_scaled.isNull() || m_scaled_size != size()) {
				m_scaled_size = size();
				m_scaled = m_picture.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			}
			painter.drawPixmap((width() - m_scaled.width()) / 2, (height() - m_scaled.height()) / 2,
			                   m_scaled);
		}
		QLinearGradient shade(area.bottomLeft(), area.topLeft());
		shade.setColorAt(0, QColor(0, 0, 0, 200));
		shade.setColorAt(0.6, QColor(0, 0, 0, 30));
		painter.fillRect(area, shade);
		if (!m_value.isEmpty()) {
			auto value_font = PixelFont(this->font(), 26, QFont::Light);
			painter.setFont(value_font);
			painter.setPen(Qt::white);
			const QRect value(16, 34, width() - 32, 34);
			painter.drawText(value, Qt::AlignLeft | Qt::AlignVCenter,
			                 QFontMetrics(value_font).elidedText(m_value, Qt::ElideRight, value.width()));
		}
		if (m_progress >= 0.0) {
			const QRectF track(16, height() - 44, width() - 32, 4);
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(255, 255, 255, 50));
			painter.drawRoundedRect(track, 2, 2);
			painter.setBrush(QColor(0x4f, 0x8c, 0xff));
			painter.drawRoundedRect(
			    QRectF(track.topLeft(), QSizeF(track.width() * std::clamp(m_progress, 0.0, 1.0), 4)), 2,
			    2);
		}
		if (!m_icon.isNull() && m_value.isEmpty()) {
			const QRect circle(16, height() - 76, 34, 34);
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(255, 255, 255, 60));
			painter.drawEllipse(circle);
			m_icon.paint(&painter, circle.adjusted(7, 7, -7, -7));
		}
		auto font = PixelFont(this->font(), 15, QFont::Normal);
		painter.setFont(font);
		painter.setPen(Qt::white);
		const QRect caption(16, height() - 36, width() - 32, 22);
		painter.drawText(caption, Qt::AlignLeft | Qt::AlignVCenter,
		                 QFontMetrics(font).elidedText(m_caption, Qt::ElideRight, caption.width()));
		if (!m_badge.isEmpty()) {
			font.setPixelSize(12);
			painter.setFont(font);
			painter.setPen(QColor(255, 255, 255, 190));
			painter.drawText(QRect(16, 12, width() - 32, 18), Qt::AlignLeft | Qt::AlignVCenter,
			                 m_badge);
		}
		painter.setClipping(false);
		if (underMouse() || hasFocus()) {
			painter.setPen(QPen(QColor(255, 255, 255, 235), 3));
			painter.setBrush(Qt::NoBrush);
			painter.drawPath(path);
		}
	}

	void mouseReleaseEvent(QMouseEvent* event) override {
		if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
			Click();
		}
	}

	void focusInEvent(QFocusEvent* event) override {
		QWidget::focusInEvent(event);
		update();
	}

	void focusOutEvent(QFocusEvent* event) override {
		QWidget::focusOutEvent(event);
		update();
	}

private:
	QColor                m_top;
	QColor                m_bottom;
	std::function<void()> m_on_click;
	QString               m_caption;
	QString               m_badge;
	QPixmap               m_picture;
	QIcon                 m_icon;
	QString               m_value;
	qreal                 m_progress = -1.0;
	QPixmap               m_scaled;
	QSize                 m_scaled_size;
};

Ps5Home::Ps5Home(QTreeWidget* list, QWidget* parent): QWidget(parent), m_list(list) {
	setStyleSheet(QStringLiteral(R"(
		QLabel { color: white; background: transparent; }
		QLabel#dim { color: rgba(255, 255, 255, 140); }
		QLabel#soft { color: rgba(255, 255, 255, 205); }
		QLineEdit { color: white; background: rgba(255, 255, 255, 34); border: none;
		            border-radius: 17px; padding: 7px 16px; font-size: 14px; }
		QToolButton { background: transparent; border: none; color: white; }
		QToolButton:hover { background: rgba(255, 255, 255, 36); border-radius: 21px; }
		QToolButton#store { background: rgba(20, 26, 40, 220); border-radius: 10px; }
		QToolButton#store:hover { background: rgba(40, 52, 80, 235); border: 3px solid white; }
		QToolButton#more { background: rgba(255, 255, 255, 46); border-radius: 23px;
		                   font-size: 20px; font-weight: 700; }
		QToolButton#more::menu-indicator { image: none; }
		QPushButton#play { background: white; color: black; border: none; border-radius: 23px;
		                   padding: 0 56px; font-size: 17px; font-weight: 600; }
		QPushButton#play:hover { background: #dfe7f5; }
		QPushButton#play:disabled { background: rgba(255, 255, 255, 80); color: rgba(0, 0, 0, 120); }
		QListView { background: transparent; border: none; outline: none; }
		QToolButton:focus { background: rgba(255, 255, 255, 46); border: 3px solid white;
		                    border-radius: 21px; }
		QToolButton#store:focus { border: 3px solid white; border-radius: 10px; }
		QToolButton#more:focus { border: 3px solid white; border-radius: 23px; }
		QPushButton#play:focus { border: 3px solid #6fa8ff; }
	)"));
	const qreal ratio = devicePixelRatioF();

	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 18, 0, 26);
	root->setSpacing(0);

	// Top bar: sections on the left; search, settings, user and clock on the right.
	auto* top   = new QHBoxLayout;
	auto* games = new QLabel(tr("Jeux"), this);
	games->setFont(PixelFont(font(), 24, QFont::DemiBold));
	auto* media = new QLabel(tr("Médias"), this);
	media->setObjectName(QStringLiteral("dim"));
	media->setFont(PixelFont(font(), 24, QFont::Normal));
	m_search = new QLineEdit(this);
	m_search->setPlaceholderText(tr("Rechercher un jeu"));
	m_search->setClearButtonEnabled(true);
	m_search->setFixedWidth(240);
	m_search->setVisible(false);
	auto* search = new QToolButton(this);
	search->setIcon(SearchIcon(ratio));
	search->setIconSize(QSize(30, 30));
	search->setFixedSize(44, 44);
	search->setToolTip(tr("Rechercher"));
	search->setCursor(Qt::PointingHandCursor);
	auto* settings = new QToolButton(this);
	settings->setIcon(GearIcon(ratio));
	settings->setIconSize(QSize(30, 30));
	settings->setFixedSize(44, 44);
	settings->setToolTip(tr("Paramètres"));
	settings->setCursor(Qt::PointingHandCursor);
	auto* avatar = new QLabel(QStringLiteral("K"), this);
	avatar->setFixedSize(36, 36);
	avatar->setAlignment(Qt::AlignCenter);
	avatar->setStyleSheet(QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:1, y2:1, "
	                                     "stop:0 #4f8cff, stop:1 #1f4fc8); border-radius: 18px; "
	                                     "font-size: 16px; font-weight: 700;"));
	m_clock = new QLabel(this);
	m_clock->setFont(PixelFont(font(), 24, QFont::Light));
	top->setContentsMargins(38, 0, 40, 0);
	top->addWidget(games);
	top->addSpacing(40);
	top->addWidget(media);
	top->addStretch();
	top->addWidget(m_search);
	top->addSpacing(6);
	top->addWidget(search);
	top->addSpacing(18);
	top->addWidget(settings);
	top->addSpacing(18);
	top->addWidget(avatar);
	top->addSpacing(34);
	top->addWidget(m_clock);
	root->addLayout(top);
	root->addSpacing(16);

	// Tile row: store tile, then the games.
	auto* row   = new QHBoxLayout;
	auto* store = new QToolButton(this);
	store->setObjectName(QStringLiteral("store"));
	store->setIcon(StoreIcon(ratio));
	store->setIconSize(QSize(44, 44));
	store->setFixedSize(TILE_SMALL, TILE_SMALL);
	store->setToolTip(tr("Store : liste de compatibilité KytyPS5"));
	store->setCursor(Qt::PointingHandCursor);
	auto* tiles = new TileRow(this);
	m_tiles     = tiles;
	m_tiles->setModel(list->model());
	m_tiles->setSelectionModel(list->selectionModel());
	m_tiles->setModelColumn(GAME_NAME_COLUMN);
	m_tiles->setFlow(QListView::LeftToRight);
	m_tiles->setWrapping(false);
	m_tiles->setSpacing(0);
	m_tiles->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_tiles->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_tiles->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
	m_tiles->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_tiles->setFrameShape(QFrame::NoFrame);
	m_tiles->setFixedHeight(TILE_LARGE + 6); // the caption sits beside the tile, not under it
	m_tiles->setItemDelegate(new TileDelegate(tiles));
	m_tiles->viewport()->setAutoFillBackground(false);
	row->setContentsMargins(22, 0, 0, 0);
	row->setSpacing(TILE_SPACING);
	row->addWidget(store, 0, Qt::AlignTop);
	row->addWidget(m_tiles, 1, Qt::AlignTop);
	root->addLayout(row);
	root->addStretch(1);

	// Selected game: source line, title, description and actions.
	// In its own widget, so a game change fades the text in (see UpdateDetails).
	m_hero     = new QWidget(this);
	auto* hero = new QVBoxLayout(m_hero);
	hero->setContentsMargins(64, 0, 64, 0);
	hero->setSpacing(6);
	m_source = new QLabel(this);
	m_source->setObjectName(QStringLiteral("soft"));
	m_source->setFont(PixelFont(font(), 15, QFont::Normal));
	m_title = new QLabel(this);
	m_title->setFont(PixelFont(font(), 40, QFont::Light));
	m_title->setWordWrap(true);
	m_title->setMaximumWidth(820);
	m_description = new QLabel(this);
	m_description->setObjectName(QStringLiteral("soft"));
	m_description->setFont(PixelFont(font(), 18, QFont::Light));
	m_description->setWordWrap(true);
	m_description->setMaximumWidth(720);
	hero->addWidget(m_source);
	hero->addSpacing(4);
	hero->addWidget(m_title);
	hero->addWidget(m_description);

	auto* actions = new QHBoxLayout;
	actions->setSpacing(14);
	m_play = new QPushButton(tr("Jouer"), this);
	m_play->setObjectName(QStringLiteral("play"));
	m_play->setFixedHeight(46);
	m_play->setCursor(Qt::PointingHandCursor);
	auto* more = new QToolButton(this);
	more->setObjectName(QStringLiteral("more"));
	more->setText(QStringLiteral("···"));
	more->setFixedSize(46, 46);
	more->setPopupMode(QToolButton::InstantPopup);
	more->setCursor(Qt::PointingHandCursor);
	auto* menu = new QMenu(more);
	menu->addAction(tr("Paramètres du jeu..."), this, &Ps5Home::EditRequested);
	menu->addAction(tr("Commandes (manette et clavier)..."), this, &Ps5Home::InputRequested);
	menu->addAction(tr("Ouvrir le dossier du jeu"), this, &Ps5Home::OpenFolderRequested);
	menu->addAction(tr("Trophées du jeu"), this, &Ps5Home::TrophiesRequested);
	menu->addAction(tr("Tous les trophées"), this, &Ps5Home::TrophyOverviewRequested);
	menu->addSeparator();
	menu->addAction(tr("Actualiser la liste des jeux"), this, &Ps5Home::RefreshRequested);
	more->setMenu(menu);
	actions->addWidget(m_play);
	actions->addWidget(more);
	actions->addStretch();
	hero->addSpacing(14);
	hero->addLayout(actions);
	root->addWidget(m_hero);
	auto* hero_opacity = new QGraphicsOpacityEffect(m_hero);
	hero_opacity->setOpacity(1.0);
	m_hero->setGraphicsEffect(hero_opacity);
	// Like the tile caption: hidden while the tiles resize, then faded in.
	m_hero_fade = new QPropertyAnimation(hero_opacity, "opacity", this);
	m_hero_fade->setDuration(TileRow::TransitionMs);
	m_hero_fade->setStartValue(0.0);
	m_hero_fade->setKeyValueAt(0.38, 0.0);
	m_hero_fade->setEndValue(1.0);
	// The effect renders offscreen; drop it at rest so idle repaints stay direct.
	connect(m_hero_fade, &QPropertyAnimation::finished, this,
	        [hero_opacity]() { hero_opacity->setEnabled(false); });
	hero_opacity->setEnabled(false);
	root->addSpacing(26);

	// Dashboard cards along the bottom, all from real data: the game's trophies, every game's
	// trophies, the connected controller, the game's disk and its settings.
	auto* cards       = new QHBoxLayout;
	m_trophy_card     = new MediaCard(this, QColor(70, 54, 18), QColor(20, 14, 6),
	                                  [this]() { emit TrophiesRequested(); });
	m_all_trophy_card = new MediaCard(this, QColor(60, 40, 90), QColor(14, 10, 26),
	                                  [this]() { emit TrophyOverviewRequested(); });
	m_pad_card        = new MediaCard(this, QColor(24, 40, 70), QColor(6, 10, 22),
	                                  [this]() { emit InputRequested(); });
	m_storage_card    = new MediaCard(this, QColor(18, 80, 90), QColor(6, 24, 30),
	                                  [this]() { emit OpenFolderRequested(); });
	m_setup_card      = new MediaCard(this, QColor(30, 90, 200), QColor(10, 30, 90),
	                                  [this]() { emit EditRequested(); });
	m_trophy_card->SetIcon(TintedIcon(QStringLiteral(":/icons/trophy.svg"), 20, ratio));
	m_setup_card->SetIcon(GearIcon(ratio));
	m_setup_card->SetContent(tr("Paramètres du jeu"), tr("CONFIGURATION"), {});
	m_all_trophy_card->SetContent(tr("Chargement..."), tr("TOUS LES TROPHÉES"), {});
	SetControllerStatus({}, -1);
	cards->setContentsMargins(64, 0, 0, 0);
	cards->setSpacing(16);
	for (auto* card: {m_trophy_card, m_all_trophy_card, m_pad_card, m_storage_card, m_setup_card}) {
		cards->addWidget(card, 1);
	}
	cards->addSpacing(24);
	root->addLayout(cards);

	// Console-style navigation: arrows (or a controller) move between these zones and within them.
	m_more   = more;
	m_zones  = {{search, settings},
	            {store, m_tiles},
	            {m_play, more},
	            {m_trophy_card, m_all_trophy_card, m_pad_card, m_storage_card, m_setup_card}};
	m_zone_index = {0, 1, 0, 0};
	for (const auto& zone: m_zones) {
		for (auto* widget: zone) {
			if (widget != m_tiles) {
				widget->setFocusPolicy(Qt::StrongFocus);
			}
			widget->installEventFilter(this);
		}
	}
	m_search->installEventFilter(this);
	new GamepadNavigator(this, [this](const QString& name, int battery) {
		SetControllerStatus(name, battery);
	});

	connect(search, &QToolButton::clicked, this, [this]() {
		const bool show = !m_search->isVisible();
		m_search->setVisible(show);
		if (show) {
			m_search->setFocus();
		} else {
			m_search->clear();
			FocusTiles();
		}
	});
	connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
		emit SearchChanged(text);
		EnsureSelection();
	});
	connect(settings, &QToolButton::clicked, this, &Ps5Home::SettingsRequested);
	connect(store, &QToolButton::clicked, this,
	        []() { QDesktopServices::openUrl(QUrl(QString::fromLatin1(STORE_URL))); });
	connect(m_play, &QPushButton::clicked, this, &Ps5Home::RunRequested);
	connect(m_tiles, &QListView::activated, this, &Ps5Home::RunRequested);
	connect(list->selectionModel(), &QItemSelectionModel::currentChanged, this,
	        [this](const QModelIndex& /*current*/, const QModelIndex& previous) {
		        static_cast<TileRow*>(m_tiles)->Transition(previous.isValid() ? previous.row() : -1);
		        UpdateDetails();
	        });
	connect(list->model(), &QAbstractItemModel::dataChanged, this, [this]() {
		UpdateDetails();
		m_tiles->viewport()->update();
	});
	// Like the console, some game is always selected once the scan has added any.
	connect(list->model(), &QAbstractItemModel::rowsInserted, this,
	        [this]() { QTimer::singleShot(0, this, &Ps5Home::EnsureSelection); });
	connect(list->model(), &QAbstractItemModel::rowsRemoved, this, &Ps5Home::UpdateDetails);

	const auto update_clock = [this]() {
		m_clock->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));
	};
	auto* clock_timer = new QTimer(this);
	connect(clock_timer, &QTimer::timeout, this, update_clock);
	clock_timer->start(10000);
	update_clock();
	UpdateDetails();
	QTimer::singleShot(0, this, &Ps5Home::FocusTiles);
}

void CollapseLayout(QLayout* layout) {
	for (int index = layout->count() - 1; index >= 0; index--) {
		auto* item = layout->itemAt(index);
		if (item->spacerItem() != nullptr) {
			delete layout->takeAt(index);
		} else if (item->layout() != nullptr) {
			CollapseLayout(item->layout());
		}
	}
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);
}

bool Ps5Home::eventFilter(QObject* watched, QEvent* event) {
	if (event->type() != QEvent::KeyPress) {
		return QWidget::eventFilter(watched, event);
	}
	auto*      widget = qobject_cast<QWidget*>(watched);
	const auto key    = static_cast<QKeyEvent*>(event)->key();
	if (watched == m_search) {
		// The search field keeps its own editing keys; Escape closes it, Down returns to the games.
		if (key == Qt::Key_Escape) {
			m_search->clear();
			m_search->setVisible(false);
			FocusTiles();
			return true;
		}
		if (key == Qt::Key_Down) {
			FocusTiles();
			return true;
		}
		return false;
	}
	switch (key) {
		case Qt::Key_Up:
		case Qt::Key_Down:
		case Qt::Key_Left:
		case Qt::Key_Right: return MoveFocus(widget, key);
		case Qt::Key_Return:
		case Qt::Key_Enter:
		case Qt::Key_Space: Activate(widget); return true;
		case Qt::Key_Escape:
			if (m_search->isVisible()) {
				m_search->clear();
				m_search->setVisible(false);
			}
			FocusTiles();
			return true;
		case Qt::Key_Menu: m_more->showMenu(); return true;
		case Qt::Key_F2: emit SettingsRequested(); return true;
		default: return QWidget::eventFilter(watched, event);
	}
}

bool Ps5Home::MoveFocus(QWidget* from, int key) {
	for (int zone = 0; zone < static_cast<int>(m_zones.size()); zone++) {
		const auto& widgets = m_zones[zone];
		for (int index = 0; index < static_cast<int>(widgets.size()); index++) {
			if (widgets[index] != from) {
				continue;
			}
			if (key == Qt::Key_Left || key == Qt::Key_Right) {
				if (from == m_tiles) {
					// The list moves between games; left of the first game is the store tile.
					int first = 0;
					while (first < m_list->topLevelItemCount() && m_tiles->isRowHidden(first)) {
						first++;
					}
					if (key == Qt::Key_Left && m_tiles->currentIndex().row() <= first) {
						FocusZone(zone, 0);
						return true;
					}
					return false;
				}
				const int next = index + (key == Qt::Key_Left ? -1 : 1);
				if (next >= 0 && next < static_cast<int>(widgets.size())) {
					FocusZone(zone, next);
				}
				return true;
			}
			const int target = zone + (key == Qt::Key_Up ? -1 : 1);
			if (target >= 0 && target < static_cast<int>(m_zones.size())) {
				FocusZone(target, m_zone_index[target]);
			}
			return true;
		}
	}
	return false;
}

void Ps5Home::FocusZone(int zone, int index) {
	const auto& widgets = m_zones[zone];
	index               = std::clamp(index, 0, static_cast<int>(widgets.size()) - 1);
	// Skip a disabled entry (Play without a game) towards the next one that takes focus.
	for (int step = 0; step < static_cast<int>(widgets.size()); step++) {
		auto* widget = widgets[(index + step) % widgets.size()];
		if (widget->isVisible() && widget->isEnabled()) {
			m_zone_index[zone] = static_cast<int>((index + step) % widgets.size());
			widget->setFocus(Qt::TabFocusReason);
			return;
		}
	}
}

void Ps5Home::Activate(QWidget* widget) {
	if (widget == m_tiles) {
		if (m_tiles->currentIndex().isValid()) {
			emit RunRequested();
		}
	} else if (auto* card = dynamic_cast<MediaCard*>(widget); card != nullptr) {
		card->Click();
	} else if (widget == m_more) {
		m_more->showMenu();
	} else if (auto* button = qobject_cast<QAbstractButton*>(widget); button != nullptr) {
		button->click();
	}
}

void Ps5Home::SetRowHidden(int row, bool hidden) {
	m_tiles->setRowHidden(row, hidden);
}

void Ps5Home::FocusTiles() {
	m_tiles->setFocus();
}

QString Ps5Home::SelectedBaseDir() const {
	const auto current = m_list->selectionModel()->currentIndex();
	auto*      item    = current.isValid()
	                         ? static_cast<ConfigurationItem*>(m_list->topLevelItem(current.row()))
	                         : nullptr;
	return item != nullptr ? item->GetInfo().basedir : QString();
}

void Ps5Home::EnsureSelection() {
	if (!m_list->selectionModel()->currentIndex().isValid()) {
		for (int row = 0; row < m_list->topLevelItemCount(); row++) {
			if (auto* item = m_list->topLevelItem(row); !item->isHidden()) {
				m_list->setCurrentItem(item);
				break;
			}
		}
	}
	UpdateDetails();
}

void Ps5Home::UpdateDetails() {
	const auto current = m_list->selectionModel()->currentIndex();
	m_play->setEnabled(current.isValid());
	if (!current.isValid()) {
		const bool empty = m_list->topLevelItemCount() == 0;
		m_source->setText(QStringLiteral("KytyPS5 Lite"));
		m_title->setText(empty ? tr("Aucun jeu trouvé") : tr("Choisis un jeu"));
		m_description->setText(empty ? tr("Ajoute un dossier de jeux avec l'engrenage.") : QString());
		m_trophy_card->SetContent(tr("Aucun jeu sélectionné"), tr("TROPHÉES DU JEU"), {});
		m_trophy_card->SetStats({});
		m_storage_card->SetContent(tr("Aucun jeu sélectionné"), tr("STOCKAGE"), {});
		m_storage_card->SetStats({});
		m_art_base_dir.clear();
		return;
	}
	const auto column = [&current](int index) {
		return current.siblingAtColumn(index).data().toString();
	};
	m_source->setText(QStringLiteral("PS5  |  %1  |  v%2")
	                      .arg(column(GAME_SERIAL_COLUMN), column(GAME_VERSION_COLUMN)));
	m_title->setText(column(GAME_NAME_COLUMN));
	m_description->setText(tr("Taille : %1   ·   Firmware requis : %2")
	                           .arg(column(GAME_SIZE_COLUMN), column(GAME_FIRMWARE_VERSION_COLUMN)));

	// Card pictures change only with the game, not on every size or status update.
	const auto base = SelectedBaseDir();
	if (base == m_art_base_dir) {
		return;
	}
	m_art_base_dir = base;
	if (isVisible()) {
		m_hero->graphicsEffect()->setEnabled(true);
		m_hero_fade->stop();
		m_hero_fade->start();
	}
	// The background already shows pic0; the card takes the small icon0.
	QPixmap icon;
	if (!base.isEmpty()) {
		icon.loadFromData(GameContent::ReadFile(base, QStringLiteral("sce_sys/icon0.png"),
		                                        GameContent::MaxImageSize));
	}
	m_trophy_card->SetContent(tr("Chargement des trophées..."), tr("TROPHÉES DU JEU"), icon);
	m_trophy_card->SetStats({});

	const QStorageInfo storage(base);
	if (storage.isValid() && storage.bytesTotal() > 0) {
		constexpr double GB   = 1000.0 * 1000.0 * 1000.0;
		const auto       free = static_cast<double>(storage.bytesAvailable());
		const auto       all  = static_cast<double>(storage.bytesTotal());
		m_storage_card->SetContent(
		    tr("%1 libres sur %2 Go").arg(QString::number(free / GB, 'f', 0), QString::number(all / GB, 'f', 0)),
		    tr("STOCKAGE  ·  %1").arg(storage.rootPath()), {});
		m_storage_card->SetStats(tr("%1 Go").arg(QString::number(free / GB, 'f', 0)), 1.0 - free / all);
	} else {
		m_storage_card->SetContent(tr("Disque introuvable"), tr("STOCKAGE"), {});
		m_storage_card->SetStats({});
	}
}

void Ps5Home::SetGameTrophies(const QString& base_dir, const Common::Trophies::Progress& progress) {
	if (base_dir != m_art_base_dir) {
		return; // the selection moved on while the trophies loaded
	}
	if (progress.total == 0) {
		m_trophy_card->SetContent(tr("Aucun trophée pour ce jeu"), tr("TROPHÉES DU JEU"), {});
		m_trophy_card->SetStats({});
		return;
	}
	// Grades 1..4: platinum, gold, silver, bronze.
	const auto& grade = progress.earned_grade;
	m_trophy_card->SetContent(tr("Platine %1  ·  Or %2  ·  Argent %3  ·  Bronze %4")
	                              .arg(grade[1])
	                              .arg(grade[2])
	                              .arg(grade[3])
	                              .arg(grade[4]),
	                          tr("TROPHÉES DU JEU  ·  %1 %").arg(progress.Percentage()), {});
	m_trophy_card->SetStats(QStringLiteral("%1 / %2").arg(progress.earned).arg(progress.total),
	                        static_cast<qreal>(progress.earned) / progress.total);
}

void Ps5Home::SetAllTrophies(const Common::Trophies::Progress& progress, int games) {
	const auto& grade = progress.earned_grade;
	m_all_trophy_card->SetContent(
	    tr("%1 jeux  ·  Platine %2  ·  Or %3").arg(games).arg(grade[1]).arg(grade[2]),
	    tr("TOUS LES TROPHÉES"), {});
	m_all_trophy_card->SetStats(
	    tr("%1 obtenus").arg(progress.earned),
	    progress.total != 0 ? static_cast<qreal>(progress.earned) / progress.total : 0.0);
}

void Ps5Home::SetControllerStatus(const QString& name, int battery) {
	if (name.isEmpty()) {
		m_pad_card->SetContent(tr("Branche une manette (USB ou Bluetooth)"), tr("MANETTE"), {});
		m_pad_card->SetStats(tr("Aucune"));
		return;
	}
	const auto power = battery > 100 ? tr("Filaire")
	                   : battery >= 0 ? tr("Batterie %1 %").arg(battery)
	                                  : tr("Batterie inconnue");
	m_pad_card->SetContent(power, tr("MANETTE CONNECTÉE"), {});
	m_pad_card->SetStats(name, battery >= 0 && battery <= 100 ? battery / 100.0 : -1.0);
}
