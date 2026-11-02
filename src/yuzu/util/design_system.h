// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QPushButton>
#include <QVariantAnimation>

namespace UIControls {

namespace Colors {
inline const QColor Background{QStringLiteral("#18181b")};
inline const QColor Surface{QStringLiteral("#252529")};
inline const QColor Control{QStringLiteral("#323238")};
inline const QColor Hover{QStringLiteral("#3c3943")};
inline const QColor Accent{QStringLiteral("#c9c3da")};
inline const QColor Text{QStringLiteral("#f3f2f5")};
inline const QColor Muted{QStringLiteral("#aaa8b2")};
} // namespace Colors

// Standard QPushButton behavior, with the same animated surface in every view.
class Button : public QPushButton {
public:
    explicit Button(QWidget* parent = nullptr);
    explicit Button(const QString& text, QWidget* parent = nullptr);
    Button(const QIcon& icon, const QString& text, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void AnimateHover(qreal target);
    QVariantAnimation hover_animation;
    qreal hover_amount{};
};

const QString& SharedStyle();
void ApplyPalette(QWidget* widget);
void BindEnabled(QWidget* source, QWidget* target);

} // namespace UIControls
