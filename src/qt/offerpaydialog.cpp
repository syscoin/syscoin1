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
OfferPayDialog::OfferPayDialog(QString offerAcceptGUID, QString offerAcceptTXID, QString notes, QWidget *parent) :
    QDialog(parent), 
	ui(new Ui::OfferPayDialog), offerAcceptGUID(offerAcceptGUID), offerAcceptTXID(offerAcceptTXID), notes(notes)
{
    ui->setupUi(this);
	this->offerPaid = false;
	this->progress = 0;
	ui->progressBar->setValue(this->progress);
	connect(ui->finishButton, SIGNAL(clicked()), this, SLOT(accept()));
	this->timer = new QTimer(this);
    connect(this->timer, SIGNAL(timeout()), this, SLOT(offerAcceptWatcher()));
    this->timer->start(10);
	ui->finishButton->setEnabled(false);

}

OfferPayDialog::~OfferPayDialog()
{
	timer->stop();
    delete ui;
}

void OfferPayDialog::offerAcceptWatcher()
{
    if(this->progress >= 100)
	{
		this->progress = 1;	
	}
	if(!lookup())
	{
		this->progress = 0;
		ui->progressBar->setValue(this->progress);
		this->timer->stop();
		ui->purchaseHint->setText(tr("There was a problem looking up this offer information, please try again later..."));
		return;
	}

	this->progress += 1;
    if(this->progress >= 100)
	{
		this->progress = 100;	
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
	params.push_back(this->offerAcceptGUID.toStdString());
	ui->purchaseHint->setText(tr("Purchase accepted. Waiting for confirmation, this may take a few minutes..."));
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
				QString paytxid = QString("");
				QString paid = QString("");
				QString priceStr = QString("");
				QString qtyStr = QString("");
				int64 qty = 0;
				uint64 price = 0;
				if (accept.type() != obj_type)
					continue;
				Object& o = accept.get_obj();
				const Value& txid_value = find_value(o, "paytxid");
				if (txid_value.type() == str_type)
					paytxid = QString::fromStdString(txid_value.get_str());
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
				if(paytxid == this->offerAcceptTXID)
				{
					if(paid == QString("true")) {
						ui->payMessage->setText(tr("<p>You've purchased %1 of '%2' for %3 SYS!</p><p><FONT COLOR='green'><b>Your payment is complete!</b></FONT></p><p>The merchant has been sent your delivery information and your item should arrive shortly. The merchant may followup with further information.</p><br><p>TxID: <b>%1</b></p>").arg(qtyStr).arg(QString::fromStdString(stringFromVch(offerOut.sTitle))).arg(priceStr).arg(paytxid));
						confirmed();
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
		return false;

	}
	catch(std::exception& e)
	{
		QMessageBox::critical(this, windowTitle(),
			tr("There was an exception trying to locate this offer, please check the offer ID and that it has been confirmed by the blockchain: ") + QString::fromStdString(e.what()),
				QMessageBox::Ok, QMessageBox::Ok);
		return false;
	}
	return true;


}
void OfferPayDialog::confirmed()
{
	ui->finishButton->setEnabled(true);
	this->offerPaid = true;
	this->offerAcceptGUID = QString("");
	this->offerAcceptTXID = QString("");
	this->progress = 100;
	this->timer->stop();				
	ui->progressBar->setValue(this->progress);
	ui->purchaseHint->setText(tr("Please click Finish"));
}
bool OfferPayDialog::getPaymentStatus()
{
	return this->offerPaid;
}

