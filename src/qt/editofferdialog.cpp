#include "editofferdialog.h"
#include "ui_editofferdialog.h"

#include "offertablemodel.h"
#include "offer.h"
#include "guiutil.h"

#include <QDataWidgetMapper>
#include <QMessageBox>

EditOfferDialog::EditOfferDialog(Mode mode, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EditOfferDialog), mapper(0), mode(mode), model(0)
{
    ui->setupUi(this);

    GUIUtil::setupAddressWidget(ui->nameEdit, this);

    switch(mode)
    {
    case NewOffer:
        setWindowTitle(tr("New offer"));
        break;
    case EditOffer:
        setWindowTitle(tr("Edit offer"));
        break;
    }

    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
}

EditOfferDialog::~EditOfferDialog()
{
    delete ui;
}

void EditOfferDialog::setModel(OfferTableModel *model)
{
    this->model = model;
    if(!model) return;

    mapper->setModel(model);
    mapper->addMapping(ui->nameEdit, OfferTableModel::Name);
    mapper->addMapping(ui->catEdit, OfferTableModel::Category);
	mapper->addMapping(ui->titleEdit, OfferTableModel::Title);
	mapper->addMapping(ui->priceEdit, OfferTableModel::Price);
	mapper->addMapping(ui->qtyEdit, OfferTableModel::Quantity);
	mapper->addMapping(ui->expEdit, OfferTableModel::ExpirationDepth);
}

void EditOfferDialog::loadRow(int row)
{
    mapper->setCurrentIndex(row);
}

bool EditOfferDialog::saveCurrentRow()
{
    if(!model) return false;

    switch(mode)
    {
    case NewOffer:
        offer = model->addRow(OfferTableModel::Offer,
                ui->nameEdit->text(),
                ui->catEdit->text(),
                ui->titleEdit->text(),
                ui->priceEdit->text(),
                ui->qtyEdit->text(),
                ui->expEdit->text());
        break;
    case EditOffer:
        if(mapper->submit())
        {
            offer = ui->nameEdit->text() + ui->catEdit->text() + ui->titleEdit->text()+ ui->priceEdit->text()+ ui->qtyEdit->text()+ui->expEdit->text();
        }
        break;
    }
    return !offer.isEmpty();
}

void EditOfferDialog::accept()
{
    if(!model) return;

    if(!saveCurrentRow())
    {
        switch(model->getEditStatus())
        {
        case OfferTableModel::OK:
            // Failed with unknown reason. Just reject.
            break;
        case OfferTableModel::NO_CHANGES:
            // No changes were made during edit operation. Just reject.
            break;
        case OfferTableModel::INVALID_OFFER:
            QMessageBox::warning(this, windowTitle(),
                tr("The entered offer \"%1\" is not a valid Syscoin offer.").arg(ui->nameEdit->text()),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case OfferTableModel::DUPLICATE_OFFER:
            QMessageBox::warning(this, windowTitle(),
                tr("The entered offer \"%1\" is already taken.").arg(ui->nameEdit->text()),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case OfferTableModel::WALLET_UNLOCK_FAILURE:
            QMessageBox::critical(this, windowTitle(),
                tr("Could not unlock wallet."),
                QMessageBox::Ok, QMessageBox::Ok);
            break;

        }
        return;
    }
    QDialog::accept();
}
QString EditOfferDialog::getOffer() const
{
    return offer;
}


