#include "resellofferdialog.h"
#include "ui_resellofferdialog.h"
#include "offertablemodel.h"
#include "guiutil.h"
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
int64 GetOfferNetworkFee(opcodetype seed, unsigned int nHeight);
ResellOfferDialog::ResellOfferDialog(QModelIndex *idx, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ResellOfferDialog)
{
    ui->setupUi(this);

	QString offerGUID = idx->data(OfferTableModel::NameRole).toString();
	ui->descriptionEdit->setPlainText(idx->data(OfferTableModel::DescriptionRole).toString());
	ui->offerGUIDLabel->setText(offerGUID);
	ui->commissionDisclaimer->setText(tr("<font color='red'>The payment of <b>commission</b> for an offer sale. Payments will be calculated on the basis of a percentage of the offer value. Enter your desired percentage.</font>"));

}

ResellOfferDialog::~ResellOfferDialog()
{
    delete ui;
}

bool ResellOfferDialog::saveCurrentRow()
{

	Array params;
	string strMethod;
	double updateFee;
	string updateFeeStr;
	QMessageBox::StandardButton retval;

    
	updateFee = (double)GetOfferNetworkFee(OP_OFFER_ACTIVATE, nBestHeight)/(double)COIN;
	updateFeeStr = strprintf("%.2f", updateFee);
    retval = QMessageBox::question(this, tr("Confirm Offer Resale"),
             tr("Warning: Reselling an offer will cost ") + QString::fromStdString(updateFeeStr) + " SYS<br><br>" + tr("Do you want to continue?"),
             QMessageBox::Yes|QMessageBox::Cancel,
             QMessageBox::Cancel);
	if(retval != QMessageBox::Yes)
	{
		return false;
	}
	strMethod = string("offerlink");
	params.push_back(ui->offerGUIDLabel->text().toStdString());
	params.push_back(ui->commissionEdit->text().toStdString());
	params.push_back(ui->descriptionEdit->toPlainText().toStdString());

	try {
        Value result = tableRPC.execute(strMethod, params);

		QMessageBox::information(this, windowTitle(),
        tr("Offer resold successfully! Check the <b>Selling</b> tab to see it after it has confirmed."),
			QMessageBox::Ok, QMessageBox::Ok);
		return true;	
		
	}
	catch (Object& objError)
	{
		string strError = find_value(objError, "message").get_str();
		QMessageBox::critical(this, windowTitle(),
		tr("Error creating new whitelist entry: \"%1\"").arg(QString::fromStdString(strError)),
			QMessageBox::Ok, QMessageBox::Ok);
	}
	catch(std::exception& e)
	{
		QMessageBox::critical(this, windowTitle(),
			tr("General exception creating new whitelist entry: \"%1\"").arg(QString::fromStdString(e.what())),
			QMessageBox::Ok, QMessageBox::Ok);
	}							

    return false;
}

void ResellOfferDialog::accept()
{
    

    if(!saveCurrentRow())
    {
        return;
    }
    QDialog::accept();
}


