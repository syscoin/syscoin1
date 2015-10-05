#include "editofferdialog.h"
#include "ui_editofferdialog.h"

#include "offertablemodel.h"
#include "guiutil.h"
#include "walletmodel.h"
#include "bitcoingui.h"
#include "ui_interface.h"
#include "bitcoinrpc.h"
#include "script.h"
#include <QDataWidgetMapper>
#include <QMessageBox>
#include <QStringList>
using namespace std;
using namespace json_spirit;
extern int nBestHeight;
extern const CRPCTable tableRPC;
extern string getCurrencyToSYSFromAlias(const vector<unsigned char> &vchCurrency, int64 &nFee, const unsigned int &nHeightToFind, QStringList& rateList);
extern vector<unsigned char> vchFromString(const std::string &str);
int64 GetOfferNetworkFee(opcodetype seed, unsigned int nHeight);
EditOfferDialog::EditOfferDialog(Mode mode, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EditOfferDialog), mapper(0), mode(mode), model(0)
{
    ui->setupUi(this);
	int64 nFee;
	QStringList rateList;

	if(getCurrencyToSYSFromAlias(vchFromString(""), nFee, nBestHeight, rateList) == "1")
	{
		QMessageBox::warning(this, windowTitle(),
			tr("Warning: SYS_RATES alias not found. No currency information available!"),
				QMessageBox::Ok, QMessageBox::Ok);
	}
    GUIUtil::setupAddressWidget(ui->nameEdit, this);
	ui->offerLabel->setVisible(true);
	ui->offerEdit->setVisible(true);
	ui->offerEdit->setEnabled(false);
	ui->currencyDisclaimer->setVisible(true);
	ui->currencyEdit->addItems(rateList);
    switch(mode)
    {
    case NewOffer:
		ui->descriptionEdit->setStyleSheet("color: rgb(0, 0, 0); background-color: rgb(255, 255, 255)");
		ui->offerLabel->setVisible(false);
		ui->offerEdit->setVisible(false);
        setWindowTitle(tr("New Offer"));
		ui->currencyDisclaimer->setText(tr("<font color='red'>You will receive payment in Syscoin equivalent to the Market-value of the currency you have selected.</font>"));
        break;
    case EditOffer:
		ui->descriptionEdit->setVisible(false);
		ui->descriptionLabel->setVisible(false);
		ui->currencyEdit->setEnabled(false);
		ui->currencyDisclaimer->setVisible(false);
        setWindowTitle(tr("Edit Offer"));
        break;
	}
    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
}

EditOfferDialog::~EditOfferDialog()
{
    delete ui;
}

void EditOfferDialog::setModel(WalletModel* walletModel, OfferTableModel *model)
{
    this->model = model;
	this->walletModel = walletModel;
    if(!model) return;

    mapper->setModel(model);
	mapper->addMapping(ui->offerEdit, OfferTableModel::Name);
    mapper->addMapping(ui->nameEdit, OfferTableModel::Title);
	mapper->addMapping(ui->categoryEdit, OfferTableModel::Category);
    mapper->addMapping(ui->priceEdit, OfferTableModel::Price);
	mapper->addMapping(ui->currencyEdit, OfferTableModel::Currency);
	mapper->addMapping(ui->qtyEdit, OfferTableModel::Qty);	
    
}

void EditOfferDialog::loadRow(int row)
{
    mapper->setCurrentIndex(row);
}

bool EditOfferDialog::saveCurrentRow()
{

    if(!model || !walletModel) return false;
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if(!ctx.isValid())
    {
		model->editStatus = OfferTableModel::WALLET_UNLOCK_FAILURE;
        return false;
    }
	Array params;
	string strMethod;
	double updateFee,activateFee;
	std::string updateFeeStr,activateFeeStr;
	QMessageBox::StandardButton retval;
    switch(mode)
    {
    case NewOffer:
        if (ui->nameEdit->text().trimmed().isEmpty()) {
            ui->nameEdit->setText("");
            QMessageBox::information(this, windowTitle(),
            tr("Empty name for Offer not allowed. Please try again"),
                QMessageBox::Ok, QMessageBox::Ok);
            return false;
        }
		activateFee = (double)GetOfferNetworkFee(OP_OFFER_ACTIVATE, nBestHeight)/(double)COIN;
		activateFeeStr = strprintf("%.2f", activateFee);
        retval = QMessageBox::question(this, tr("Confirm Offer Activation"),
            tr("Warning: New offer will cost ") + QString::fromStdString(activateFeeStr) + " SYS<br><br>" + tr("Are you sure you want to create this offer?"),
				 QMessageBox::Yes|QMessageBox::Cancel,
				 QMessageBox::Cancel);
		if(retval != QMessageBox::Yes)
		{
			return false;
		}
		strMethod = string("offernew");
		params.push_back(ui->categoryEdit->text().toStdString());
		params.push_back(ui->nameEdit->text().toStdString());
		params.push_back(ui->qtyEdit->text().toStdString());
		params.push_back(ui->priceEdit->text().toStdString());
		params.push_back(ui->descriptionEdit->toPlainText().toStdString());
		params.push_back(ui->currencyEdit->currentText().toStdString());
		try {
            Value result = tableRPC.execute(strMethod, params);
			Array arr = result.get_array();
			string strResult = arr[0].get_str();
			offer = ui->nameEdit->text();

			QMessageBox::information(this, windowTitle(),
            tr("New Offer created successfully! TXID: \"%1\"").arg(QString::fromStdString(strResult)),
				QMessageBox::Ok, QMessageBox::Ok);
				
			
		}
		catch (Object& objError)
		{
			string strError = find_value(objError, "message").get_str();
			QMessageBox::critical(this, windowTitle(),
			tr("Error creating new Offer: \"%1\"").arg(QString::fromStdString(strError)),
				QMessageBox::Ok, QMessageBox::Ok);
			break;
		}
		catch(std::exception& e)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("General exception creating new Offer: \"%1\"").arg(QString::fromStdString(e.what())),
				QMessageBox::Ok, QMessageBox::Ok);
			break;
		}							

        break;
    case EditOffer:
        if(mapper->submit())
        {
			updateFee = (double)GetOfferNetworkFee(OP_OFFER_UPDATE, nBestHeight)/(double)COIN;
			updateFeeStr = strprintf("%.2f", updateFee);
            retval = QMessageBox::question(this, tr("Confirm Offer Update"),
                tr("Warning: Updating a offer will cost ") + QString::fromStdString(updateFeeStr) + " SYS<br><br>" + tr("Are you sure you wish update this offer?"),
					 QMessageBox::Yes|QMessageBox::Cancel,
					 QMessageBox::Cancel);
			if(retval != QMessageBox::Yes)
			{
				return false;
			}
			strMethod = string("offerupdate");
			params.push_back(ui->offerEdit->text().toStdString());
			params.push_back(ui->categoryEdit->text().toStdString());
			params.push_back(ui->nameEdit->text().toStdString());
			params.push_back(ui->qtyEdit->text().toStdString());
			params.push_back(ui->priceEdit->text().toStdString());
			
			try {
				Value result = tableRPC.execute(strMethod, params);
				if (result.type() != null_type)
				{
					string strResult = result.get_str();
					offer = ui->nameEdit->text() + ui->offerEdit->text();

					QMessageBox::information(this, windowTitle(),
                    tr("Offer updated successfully! TXID: \"%1\"").arg(QString::fromStdString(strResult)),
						QMessageBox::Ok, QMessageBox::Ok);
						
				}
			}
			catch (Object& objError)
			{
				string strError = find_value(objError, "message").get_str();
				QMessageBox::critical(this, windowTitle(),
				tr("Error updating Offer: \"%1\"").arg(QString::fromStdString(strError)),
					QMessageBox::Ok, QMessageBox::Ok);
				break;
			}
			catch(std::exception& e)
			{
				QMessageBox::critical(this, windowTitle(),
					tr("General exception updating Offer: \"%1\"").arg(QString::fromStdString(e.what())),
					QMessageBox::Ok, QMessageBox::Ok);
				break;
			}	
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
                tr("The entered offer \"%1\" is not a valid Syscoin Offer.").arg(ui->offerEdit->text()),
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
