#include "offeracceptinfodialog.h"
#include "ui_offeracceptinfodialog.h"
#include "init.h"
#include "util.h"
#include "offer.h"
#include "bitcoingui.h"
#include "bitcoinrpc.h"
#include "monitoreddatamapper.h"
#include "offeraccepttablemodel.h"
#include <QMessageBox>
#include <QModelIndex>
#include <QDateTime>

using namespace std;
using namespace json_spirit;
extern const CRPCTable tableRPC;

OfferAcceptInfoDialog::OfferAcceptInfoDialog(const QModelIndex &idx, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OfferAcceptInfoDialog)
{
    ui->setupUi(this);

    mapper = new MonitoredDataMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
	offerGUID = idx.data(OfferAcceptTableModel::NameRole).toString();
	offerAcceptGUID = idx.data(OfferAcceptTableModel::GUIDRole).toString();
	ui->paytxidEdit->setVisible(false);
	ui->refundTXIDLabel->setVisible(false);

	ui->linkGUIDEdit->setVisible(false);
	ui->linkGUIDLabel->setVisible(false);
	ui->commissionEdit->setVisible(false);
	ui->commissionLabel->setVisible(false);

	lookup();
}

OfferAcceptInfoDialog::~OfferAcceptInfoDialog()
{
    delete ui;
}
void OfferAcceptInfoDialog::on_okButton_clicked()
{
    mapper->submit();
    accept();
}
bool OfferAcceptInfoDialog::lookup()
{
	string strError;
	string strMethod = string("offerinfo");
	Array params;
	Value result;
	params.push_back(offerGUID.toStdString());

    try {
        result = tableRPC.execute(strMethod, params);

		if (result.type() == obj_type)
		{
			Value offerAcceptsValue = find_value(result.get_obj(), "accepts");
			if(offerAcceptsValue.type() != array_type)
				return false;
			QString linkedStr = QString::fromStdString(find_value(result.get_obj(), "offerlink").get_str());
			if(linkedStr == QString("true"))
			{

				ui->linkGUIDEdit->setVisible(true);
				ui->linkGUIDLabel->setVisible(true);
				ui->commissionEdit->setVisible(true);
				ui->commissionLabel->setVisible(true);
				ui->linkGUIDEdit->setText(QString::fromStdString(find_value(result.get_obj(), "offerlink_guid").get_str()));
				ui->commissionEdit->setText(QString::fromStdString(find_value(result.get_obj(), "commission").get_str()));
			}
			Array offerAccepts = offerAcceptsValue.get_array();
			COfferAccept myAccept;
			QDateTime timestamp;
			BOOST_FOREACH(Value& accept, offerAccepts)
			{					
				Object acceptObj = accept.get_obj();
				QString offerAcceptHash = QString::fromStdString(find_value(acceptObj, "id").get_str());
				if(offerAcceptHash != offerAcceptGUID)
					continue;
				ui->guidEdit->setText(offerAcceptHash);
				ui->txidEdit->setText(QString::fromStdString(find_value(acceptObj, "txid").get_str()));
				ui->heightEdit->setText(QString::fromStdString(find_value(acceptObj, "height").get_str()));
				int unixTime = atoi(find_value(acceptObj, "time").get_str().c_str());
				timestamp.setTime_t(unixTime);
				ui->timeEdit->setText(timestamp.toString());

				ui->quantityEdit->setText(QString::fromStdString(find_value(acceptObj, "quantity").get_str()));
				ui->currencyEdit->setText(QString::fromStdString(find_value(acceptObj, "currency").get_str()));
				ui->priceEdit->setText(QString::fromStdString(find_value(acceptObj, "price").get_str()));
				ui->totalEdit->setText(QString::fromStdString(find_value(acceptObj, "total").get_str()));
				ui->paidEdit->setText(QString::fromStdString(find_value(acceptObj, "paid").get_str()));
				
				QString refundedStr = QString::fromStdString(find_value(acceptObj, "refunded").get_str());	
				
				ui->refundedEdit->setText(refundedStr);
				ui->payservicefeeEdit->setText(QString::fromStdString(find_value(acceptObj, "pay_service_fee").get_str()));
				if(refundedStr == QString("true"))
				{
					ui->paytxidEdit->setVisible(true);
					ui->refundTXIDLabel->setVisible(true);
					ui->paytxidEdit->setText(QString::fromStdString(find_value(acceptObj, "refund_txid").get_str()));
				}
				ui->paymessageEdit->setText(QString::fromStdString(find_value(acceptObj, "pay_message").get_str()));

			}

			ui->titleEdit->setText(QString::fromStdString(find_value(result.get_obj(), "title").get_str()));
			
			return true;
		}
		 

	}
	catch (Object& objError)
	{
		QMessageBox::critical(this, windowTitle(),
				tr("Could not find this offer or offer accept, please check the offer ID and that it has been confirmed by the blockchain"),
				QMessageBox::Ok, QMessageBox::Ok);

	}
	catch(std::exception& e)
	{
		QMessageBox::critical(this, windowTitle(),
			tr("There was an exception trying to locate this offer or offeraccept, please check the offer ID and that it has been confirmed by the blockchain: ") + QString::fromStdString(e.what()),
				QMessageBox::Ok, QMessageBox::Ok);
	}
	return false;


}

