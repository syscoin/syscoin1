#include "editofferdialog.h"
#include "ui_editofferdialog.h"

#include "offertablemodel.h"
#include "guiutil.h"

#include <QDataWidgetMapper>
#include <QMessageBox>

EditOfferDialog::EditOfferDialog(Mode mode, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EditOfferDialog), mapper(0), mode(mode), model(0)
{
    ui->setupUi(this);

    GUIUtil::setupAddressWidget(ui->offerEdit, this);

    switch(mode)
    {
    case NewOfferAccept:
        setWindowTitle(tr("New offer accept"));
        //ui->offerEdit->setEnabled(false);
        break;
    case NewOffer:
        setWindowTitle(tr("New offer"));
        break;
    case EditOfferAccept:
        setWindowTitle(tr("Edit offer accept"));
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
    mapper->addMapping(ui->labelEdit, OfferTableModel::Title);
    mapper->addMapping(ui->offerEdit, OfferTableModel::Name);
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
    case NewOfferAccept:
    case NewOffer:
        offer = model->addRow(
                mode == NewOffer ? OfferTableModel::Offer : OfferTableModel::OfferAccept,
                ui->labelEdit->text(),
                ui->offerEdit->text(),
                ui->offerEdit->text(),
                ui->offerEdit->text(),
                ui->offerEdit->text(),
                ui->offerEdit->text());
        break;
    case EditOfferAccept:
    case EditOffer:
        if(mapper->submit())
        {
            offer = ui->offerEdit->text();
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
                tr("The entered offer \"%1\" is not a valid Syscoin offer.").arg(ui->offerEdit->text()),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case OfferTableModel::DUPLICATE_OFFER:
            QMessageBox::warning(this, windowTitle(),
                tr("The entered offer \"%1\" is already taken.").arg(ui->offerEdit->text()),
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

void EditOfferDialog::setOffer(const QString &offer)
{
    this->offer = offer;
    ui->offerEdit->setText(offer);
}
