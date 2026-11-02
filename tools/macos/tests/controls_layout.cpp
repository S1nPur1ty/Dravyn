// SPDX-License-Identifier: GPL-3.0-or-later
// UI geometry checks only; no emulator or game is started.
#include <QApplication>
#include <QDialog>
#include <QStandardItemModel>
#include "ui_lobby.h"
#include "yuzu/util/animated_icon_button.h"
#include <QFile>
#include <QListView>
#include <QPushButton>
#include <QSlider>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>
#include "yuzu/util/popup.h"
#include <cstdio>
#include <cstdlib>
#define REQUIRE(test) do { if (!(test)) { std::fprintf(stderr, "%s failed at %d\n", #test, __LINE__); std::exit(1); } } while (0)

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QWidget window;
    window.setGeometry(50, 50, 500, 500);
    QFile style(QStringLiteral(":/library/style.qss"));
    REQUIRE(style.open(QIODevice::ReadOnly));
    window.setStyleSheet(QString::fromUtf8(style.readAll()));
    UIControls::LibraryComboBox combo(&window);
    combo.setGeometry(40, 40, 200, 40);
    combo.setView(new QListView(&combo));
    combo.addItems({QStringLiteral("Recently played"), QStringLiteral("Name A–Z"), QStringLiteral("Name Z–A")});
    combo.setCurrentIndex(2);
    window.show();
    QTest::qWait(20);
    auto* dropdown_arrow = combo.findChild<QPushButton*>(QStringLiteral("LibraryDropdownArrow"));
    REQUIRE(dropdown_arrow);
    const auto dropdown_down = dropdown_arrow->icon().pixmap(16, 16).toImage();
    combo.showPopup();
    QTest::qWait(220);
    REQUIRE(dropdown_down != dropdown_arrow->icon().pixmap(16, 16).toImage());
    auto* popup = combo.view()->window();
    QTest::qWait(20);
    REQUIRE(popup->isVisible());
    REQUIRE(popup->geometry().topLeft() == combo.mapToGlobal(QPoint(0, combo.height() + 6)));
    REQUIRE(popup->width() >= combo.width());
    REQUIRE(!popup->mask().isEmpty());
    QTest::keyClick(combo.view(), Qt::Key_Up);
    QTest::keyClick(combo.view(), Qt::Key_Return);
    REQUIRE(combo.currentIndex() == 1);
    REQUIRE(!popup->isVisible());
    QTest::qWait(220);
    REQUIRE(dropdown_down == dropdown_arrow->icon().pixmap(16, 16).toImage());

    QPushButton button(QStringLiteral("VOLUME: 100%"), &window);
    button.setGeometry(40, 400, 120, 28);
    button.show();
    QMenu menu;
    menu.addAction(QStringLiteral("Mute"));
    menu.addAction(QStringLiteral("Reset Volume"));
    bool checked = false;
    QTimer::singleShot(50, &menu, [&] {
        REQUIRE(menu.isVisible());
        REQUIRE(menu.pos().x() == button.mapToGlobal(QPoint(0, 0)).x());
        REQUIRE(menu.geometry().bottom() < button.mapToGlobal(QPoint(0, 0)).y());
        checked = true;
        menu.close();
    });
    UIControls::ExecStatusMenu(menu, &button);
    REQUIRE(checked);

    UIControls::RoundedPopup volume(&window);
    volume.setObjectName(QStringLiteral("VolumePopup"));
    volume.setStyleSheet(UIControls::PopupStyle());
    volume.setFixedWidth(240);
    auto* layout = new QVBoxLayout(&volume);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->addWidget(new QSlider(Qt::Horizontal, &volume));
    volume.adjustSize();
    volume.move(UIControls::PopupPosition(&button, volume.size(), false));
    volume.show();
    REQUIRE(volume.width() == 240 && volume.height() > button.height());
    REQUIRE(volume.geometry().bottom() < button.mapToGlobal(QPoint(0, 0)).y());
    REQUIRE(volume.testAttribute(Qt::WA_TranslucentBackground));
    volume.grab().save(QString::fromLocal8Bit(argv[0]) + QStringLiteral("-volume.png"));
    volume.hide();
    combo.move(40, 460);
    combo.showPopup();
    REQUIRE(combo.screen()->availableGeometry().contains(popup->geometry()));
    combo.hidePopup();
    AnimatedIconButton animated(&window);
    animated.setIconSize(QSize(24, 24));
    animated.SetGlyph(QIcon(QStringLiteral(":/library/refresh.svg")));
    animated.show();
    animated.SetSpinning(true);
    const auto first_frame = animated.icon().pixmap(24, 24).toImage();
    QTest::qWait(130);
    REQUIRE(first_frame != animated.icon().pixmap(24, 24).toImage());
    animated.SetSpinning(false);
    const auto stopped_frame = animated.icon().pixmap(24, 24).toImage();
    QTest::qWait(80);
    REQUIRE(stopped_frame == animated.icon().pixmap(24, 24).toImage());
    animated.SetGlyph(QIcon(QStringLiteral(":/library/chevron-down.svg")));
    const auto down = animated.icon().pixmap(24, 24).toImage();
    animated.SetExpanded(true);
    QTest::qWait(220);
    REQUIRE(down != animated.icon().pixmap(24, 24).toImage());
    animated.SetExpanded(false);
    QTest::qWait(220);
    REQUIRE(down == animated.icon().pixmap(24, 24).toImage());

    AnimatedIconButton hover_button(&window);
    hover_button.setFixedSize(40, 40);
    hover_button.SetAnimatedBackground(QColor("#222226"), QColor("#303036"), QColor("#303035"));
    hover_button.show();
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&hover_button, &leave);
    QTest::qWait(220);
    const auto idle_color = hover_button.grab().toImage().pixelColor(4, 20);
    QEnterEvent enter(QPointF(4, 20), QPointF(4, 20), QPointF(4, 20));
    QApplication::sendEvent(&hover_button, &enter);
    QTest::qWait(90);
    const auto middle_color = hover_button.grab().toImage().pixelColor(4, 20);
    QTest::qWait(150);
    const auto hover_color = hover_button.grab().toImage().pixelColor(4, 20);
    REQUIRE(idle_color != middle_color && middle_color != hover_color);
    QApplication::sendEvent(&hover_button, &leave);
    QTest::qWait(220);
    REQUIRE(idle_color == hover_button.grab().toImage().pixelColor(4, 20));

    QDialog rooms;
    Ui::Lobby lobby;
    lobby.setupUi(&rooms);
    QFile network_style(QStringLiteral(":/library/network.qss"));
    REQUIRE(network_style.open(QIODevice::ReadOnly));
    rooms.setStyleSheet(QString::fromUtf8(network_style.readAll()));
    lobby.nickname->setText(QStringLiteral("Player"));
    lobby.nickname->setFixedHeight(40);
    lobby.search->setFixedHeight(40);
    lobby.refresh_list->setFixedHeight(40);
    lobby.refresh_list->setMinimumWidth(118);
    lobby.refresh_list->setIconSize(QSize(18, 18));
    lobby.refresh_list->SetGlyph(QIcon(QStringLiteral(":/library/refresh.svg")));
    QStandardItemModel room_model;
    room_model.setHorizontalHeaderLabels({QStringLiteral("Preferred game"), QStringLiteral("Room name")});
    room_model.appendRow({new QStandardItem(QStringLiteral("Mario Kart World")), new QStandardItem(QStringLiteral("Weekend races"))});
    room_model.appendRow({new QStandardItem(QStringLiteral("Pokopia")), new QStandardItem(QStringLiteral("Friends room"))});
    lobby.room_list->setModel(&room_model);
    lobby.room_list->setColumnWidth(0, 370);
    lobby.room_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    lobby.room_list->selectionModel()->select(room_model.index(0, 0),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    lobby.games_owned->setChecked(true);
    rooms.show();
    QTest::qWait(30);
    REQUIRE(lobby.search->width() > 150);
    REQUIRE(lobby.room_list->geometry().top() > lobby.hide_full->geometry().bottom());
    const auto row_image = lobby.room_list->viewport()->grab().toImage();
    const auto row_rect = lobby.room_list->visualRect(room_model.index(0, 0));
    REQUIRE(row_image.pixelColor(369, row_rect.top() + 5) == QColor("#383541"));
    REQUIRE(row_image.pixelColor(371, row_rect.top() + 5) == QColor("#383541"));
    REQUIRE(row_image.pixelColor(5, row_rect.center().y()) == QColor("#383541"));
    rooms.grab().save(QString::fromLocal8Bit(argv[0]) + QStringLiteral("-rooms.png"));
    std::puts("PASS: refresh animation start/stop, arrow open/close, network layout;  dropdown below its button, keyboard selection, rounded popup, aligned status menu, volume sizing, screen-edge containment");
}
