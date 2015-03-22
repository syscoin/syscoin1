#include "init.h"
#include "util.h"
#include "offerpaydialog.h"
#include "ui_offerpaydialog.h"

#include "bitcoingui.h"
#include "bitcoinrpc.h"
#include <QProgressBar> 
#include <QTimer>
#include <QMessageBox>
using namespace std;
using namespace json_spirit;
extern const CRPCTable tableRPC;
OfferPayDialog::OfferPayDialog(QString offerID, QString offerAcceptGUID, QString offerAcceptTXID, QString notes, QWidget *parent) :
    QDialog(parent), 
	ui(new Ui::OfferPayDialog), offerID(offerID), offerAcceptGUID(offerAcceptGUID), offerAcceptTXID(offerAcceptTXID), notes(notes)
{
    ui->setupUi(this);
	this->offerPaid = false;
	this->progress = 0;
	ui->progressBar->setValue(this->progress);
	connect(ui->payButton, SIGNAL(clicked()), this, SLOT(pay()));
	this->timer = new QTimer(this);
    connect(this->timer, SIGNAL(timeout()), this, SLOT(offerAcceptWatcher()));
    this->timer->start(10);
	ui->payButton->setEnabled(false);

}

OfferPayDialog::~OfferPayDialog()
{
	timer->stop();
    delete ui;
}
// send offeraccept with offer guid/qty as params and then send offerpay with wtxid (first param of response) as param, using RPC commands.
void OfferPayDialog::pay()
{

		Array params;
		Value valError;
		Object ret ;
		Value valResult;
		Array arr;
		Value valId;
		Value result ;
		string strReply;
		string strError;
		string strMethod = string("offerpay");
		QString payTXID = QString("");
		if(this->notes == QString(""))
		{
			QMessageBox::critical(this, windowTitle(),
				tr("You must enter a note to the seller, usually shipping address if the offer is for a physical item!"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		if(this->offerAcceptGUID == QString(""))
		{
			QMessageBox::critical(this, windowTitle(),
				tr("You must accept the offer first!"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		this->offerPaid = false;
		params.push_back(this->offerAcceptGUID.toStdString());
		params.push_back(this->notes.toStdString());
	    try {
            result = tableRPC.execute(strMethod, params);
			if (result.type() == array_type)
			{
				arr = result.get_array();				
				payTXID = QString::fromStdString(arr[0].get_str());
				if(payTXID != QString(""))
				{
					this->offerPaid = true;
				}
			}
		}
		catch (Object& objError)
		{
			strError = find_value(objError, "message").get_str();
			QMessageBox::critical(this, windowTitle(),
			tr("Error paying for offer: \"%1\"").arg(QString::fromStdString(strError)),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		catch(std::exception& e)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("General exception when trying to pay for offer"),
				QMessageBox::Ok, QMessageBox::Ok);
					return;
		}
			
		if(this->offerPaid)
		{
			QMessageBox::information(this, tr("Purchase Complete"),
				tr("Your payment is complete!  The merchant has been sent your encrypted shipping information and your item should arrive shortly. The merchant may followup with further information. TxID: %1").arg(payTXID),
				QMessageBox::Ok, QMessageBox::Ok);	
			if(this->offerPaid)
			{
				this->offerAcceptGUID = QString("");
				this->offerAcceptTXID = QString("");
			}
			OfferPayDialog::accept();
		}
		else
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Offer pay returned empty result!"),
				QMessageBox::Ok, QMessageBox::Ok);
		}
   

}
void OfferPayDialog::offerAcceptWatcher()
{
	lookup();
	this->progress += 1;
	if(this->progress >= 100)
	{	
		this->progress = 0;
	}
	if(this->offerPaid)
	{
		this->progress = 100;
		this->timer->stop();
	}
	ui->progressBar->setValue(this->progress);
}
bool OfferPayDialog::lookup()
{

	string strError;
	string strMethod = string("offerinfo");
	Array params;
	Value result;
	params.push_back(this->offerID.toStdString());
	ui->purchaseHint->setText(tr("Purchase accepted. Please wait for 1 confirmation..."));
    try {
        result = tableRPC.execute(strMethod, params);

		if (result.type() == obj_type)
		{
			Object offerObj;
			offerObj = result.get_obj();
			COffer offerOut;
			offerOut.vchRand = vchFromString(find_value(offerObj, "id").get_str());
			offerOut.sTitle = vchFromString(find_value(offerObj, "title").get_str());
			offerOut.sCategory = vchFromString(find_value(offerObj, "category").get_str());
			offerOut.nPrice = QString::number(find_value(offerObj, "price").get_real()).toLongLong();
			offerOut.nQty = QString::fromStdString(find_value(offerObj, "quantity").get_str()).toLong();
			const Value& acceptsValue = find_value(offerObj, "accepts");
			if(acceptsValue.type() == array_type)
			{
			  Array arr = acceptsValue.get_array();
			  BOOST_FOREACH(Value& accept, arr)
			  {
				QString txid = QString("");
				QString paid = QString("");
				QString priceStr = QString("");
				QString qtyStr = QString("");
				int64 qty = 0;
				uint64 price = 0;
				if (accept.type() != obj_type)
					continue;
				Object& o = accept.get_obj();
				const Value& txid_value = find_value(o, "txid");
				if (txid_value.type() == str_type)
					txid = QString::fromStdString(txid_value.get_str());
				const Value& paid_value = find_value(o, "paid");
				if (paid_value.type() == str_type)
					paid = QString::fromStdString(paid_value.get_str());
				const Value& qty_value = find_value(o, "quantity");
				if (qty_value.type() == str_type)
					qtyStr = QString::fromStdString(qty_value.get_str());
				const Value& price_value = find_value(o, "price");
				if (price_value.type() == real_type)
					priceStr = QString::number(price_value.get_real());
				
				qty = qtyStr.toLong();
				price = priceStr.toLongLong()*qty;
				priceStr = QString::number(price);
				if(txid == this->offerAcceptTXID)
				{
					if(paid == QString("true")) {
						QMessageBox::critical(this, windowTitle(),
								tr("You have already paid for this offer!"),
								QMessageBox::Ok, QMessageBox::Ok);
						this->offerPaid = true;
						this->offerAcceptGUID = QString("");
						this->offerAcceptTXID = QString("");
						this->progress = 100;
						this->timer->stop();				
						ui->progressBar->setValue(this->progress);
						OfferPayDialog::accept();
						return true;
					}
					else
					{
						ui->payMessage->setText(tr("You've purchased %1 %2 for %3 SYS!").arg(qtyStr).arg(QString::fromStdString(stringFromVch(offerOut.sTitle))).arg(priceStr));
						ui->purchaseHint->setText(tr("Please click the button below to pay for your item"));
						ui->payButton->setEnabled(true);
						this->progress = 100;
						this->timer->stop();				
						ui->progressBar->setValue(this->progress);
						return true;
					}
				}
			  }	
			}
		}
		 

	}
	catch (Object& objError)
	{
		QMessageBox::critical(this, windowTitle(),
				tr("Could not find this offer, please check the offer ID and that it has been confirmed by the blockchain"),
				QMessageBox::Ok, QMessageBox::Ok);

	}
	catch(std::exception& e)
	{
		QMessageBox::critical(this, windowTitle(),
			tr("There was an exception trying to locate this offer, please check the offer ID and that it has been confirmed by the blockchain: ") + QString::fromStdString(e.what()),
				QMessageBox::Ok, QMessageBox::Ok);
	}
	return false;


}
bool OfferPayDialog::getPaymentStatus()
{
	return this->offerPaid;
}

