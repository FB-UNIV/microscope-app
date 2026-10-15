#pragma once

#include "catalog.hpp"
#include "vue_store.hpp"

#include <QWidget>

#include <optional>

class QComboBox;
class QLabel;

namespace msapp {

// Department → TP → Échantillon, picked before capturing (issue #19). With
// an empty catalog it says so and every Vue goes to Unsorted (ADR 0008).
class LocationPicker : public QWidget {
    Q_OBJECT

public:
    explicit LocationPicker(QWidget* parent = nullptr);

    // Keeps the current pick when the new catalog still has it.
    void setCatalog(const Catalog& catalog);
    const Catalog& catalog() const { return catalog_; }
    // Picks `location`'s Department, TP and Échantillon, as far as the
    // catalog has them.
    void setLocation(const VueLocation& location);

    // Where the next Vue goes: Unsorted when the catalog is empty; empty when
    // the picked TP has no Échantillon to file it under.
    std::optional<VueLocation> location() const;
    // The picked TP, for its instructions; nullptr with an empty catalog.
    const Tp* tp() const;

signals:
    void locationChanged();

private:
    void fillTps();
    void fillEchantillons();

    Catalog catalog_;
    QComboBox* department_;
    QComboBox* tp_;
    QComboBox* echantillon_;
    QLabel* noCatalog_;
    bool filling_ = false;  // no locationChanged while refilling
};

} // namespace msapp
