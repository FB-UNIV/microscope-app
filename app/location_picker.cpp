#include "location_picker.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

namespace msapp {

namespace {

// Refills `combo` with `names`, keeping `keep` selected when it is there.
template <typename Item>
void refill(QComboBox* combo, const std::vector<Item>& items, const QString& keep) {
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const auto& item : items) combo->addItem(QString::fromStdString(item.name));
    const int index = combo->findText(keep);
    combo->setCurrentIndex(index >= 0 ? index : (combo->count() > 0 ? 0 : -1));
}

} // namespace

LocationPicker::LocationPicker(QWidget* parent) : QWidget(parent) {
    department_ = new QComboBox;
    tp_ = new QComboBox;
    echantillon_ = new QComboBox;
    for (QComboBox* combo : {department_, tp_, echantillon_}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        combo->setMinimumContentsLength(8);
    }
    noCatalog_ = new QLabel(tr("Aucun catalogue de TP : les Vues sont rangées dans « Non classé »."));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(tr("Département")));
    layout->addWidget(department_);
    layout->addWidget(new QLabel(tr("TP")));
    layout->addWidget(tp_);
    layout->addWidget(new QLabel(tr("Échantillon")));
    layout->addWidget(echantillon_);
    layout->addWidget(noCatalog_);

    connect(department_, &QComboBox::currentIndexChanged, this, [this] {
        fillTps();
        if (!filling_) emit locationChanged();
    });
    connect(tp_, &QComboBox::currentIndexChanged, this, [this] {
        fillEchantillons();
        if (!filling_) emit locationChanged();
    });
    connect(echantillon_, &QComboBox::currentIndexChanged, this, [this] {
        if (!filling_) emit locationChanged();
    });

    setCatalog(Catalog{});
}

void LocationPicker::setCatalog(const Catalog& catalog) {
    const QString department = department_->currentText();
    const QString tp = tp_->currentText();
    const QString echantillon = echantillon_->currentText();

    catalog_ = catalog;
    filling_ = true;
    refill(department_, catalog_.departments, department);
    fillTps();
    {
        const QSignalBlocker blocker(tp_);
        const int index = tp_->findText(tp);
        if (index >= 0) tp_->setCurrentIndex(index);
    }
    fillEchantillons();
    {
        const QSignalBlocker blocker(echantillon_);
        const int index = echantillon_->findText(echantillon);
        if (index >= 0) echantillon_->setCurrentIndex(index);
    }
    filling_ = false;

    const bool empty = catalog_.empty();
    for (QWidget* w : findChildren<QWidget*>()) w->setVisible(!empty);
    noCatalog_->setVisible(empty);
    emit locationChanged();
}

void LocationPicker::setLocation(const VueLocation& location) {
    // Each pick refills the combos after it.
    const auto pick = [](QComboBox* combo, const std::string& name) {
        const int index = combo->findText(QString::fromStdString(name));
        if (index >= 0) combo->setCurrentIndex(index);
    };
    filling_ = true;
    pick(department_, location.department);
    pick(tp_, location.tp);
    pick(echantillon_, location.echantillon);
    filling_ = false;
    emit locationChanged();
}

void LocationPicker::fillTps() {
    const Department* department = catalog_.department(department_->currentText().toStdString());
    refill(tp_, department ? department->tps : std::vector<Tp>{}, tp_->currentText());
    fillEchantillons();
}

void LocationPicker::fillEchantillons() {
    const Tp* picked = tp();
    refill(echantillon_, picked ? picked->echantillons : std::vector<Echantillon>{}, echantillon_->currentText());
}

const Tp* LocationPicker::tp() const {
    return catalog_.tp(department_->currentText().toStdString(), tp_->currentText().toStdString());
}

std::optional<VueLocation> LocationPicker::location() const {
    if (catalog_.empty()) return VueLocation::unsorted();
    if (department_->currentIndex() < 0 || tp_->currentIndex() < 0 || echantillon_->currentIndex() < 0) {
        return std::nullopt;
    }
    return VueLocation{department_->currentText().toStdString(), tp_->currentText().toStdString(),
                       echantillon_->currentText().toStdString()};
}

} // namespace msapp
