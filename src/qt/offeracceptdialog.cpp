#include "offeracceptdialog.h"
#include "ui_offeracceptdialog.h"
#include "init.h"
#include "util.h"
#include "offerpaydialog.h"
#include "offer.h"
#include "bitcoingui.h"
#include "bitcoinrpc.h"
#include <QMessageBox>
using namespace std;
using namespace json_spirit;
extern const CRPCTable tableRPC;

OfferAcceptDialog::OfferAcceptDialog(COffer& offer, long quantity, QString notes, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OfferAcceptDialog), offer(offer), notes(notes), quantity(quantity)
{
    ui->setupUi(this);
	ui->acceptMessage->setText(tr("There was a problem accepting this offer, please try again..."));

	QString qtyStr = QString::number(this->quantity);
	QString priceStr = QString::number(this->quantity*this->offer.nPrice);
	ui->acceptMessage->setText(tr("Are you sure you want to purchase %1 of '%2'? You will be charged %3 SYS").arg(qtyStr).arg(QString::fromStdString(stringFromVch(this->offer.sTitle))).arg(priceStr));
	
	this->offerPaid = false;
	this->offerAcceptGUID = QString("");
	this->offerAcceptTXID = QString("");
	connect(ui->acceptButton, SIGNAL(clicked()), this, SLOT(acceptOffer()));
}

OfferAcceptDialog::~OfferAcceptDialog()
{
    delete ui;
}
// send offeraccept with offer guid/qty as params and then send offerpay with wtxid (first param of response) as param, using RPC commands.
void OfferAcceptDialog::acceptOffer()
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
		string strMethod = string("offeraccept");
		if(this->quantity <= 0)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Invalid quantity when trying to accept offer!"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		this->offerPaid = false;
		if(this->offerAcceptGUID != QString("") && this->offerAcceptTXID != QString(""))
		{
			OfferPayDialog dlg(QString::fromStdString(stringFromVch(this->offer.vchRand)), this->offerAcceptGUID, this->offerAcceptTXID,this->notes, this);
			if(dlg.exec())
			{
				this->offerPaid = dlg.getPaymentStatus();
				if(this->offerPaid)
				{
					this->offerAcceptGUID = QString("");
					this->offerAcceptTXID = QString("");
				}
				OfferAcceptDialog::accept();
			}
			else
			{
				OfferAcceptDialog::close();
			}
			

			return;
		}
		params.push_back(stringFromVch(this->offer.vchRand));
		params.push_back(QString::number(this->quantity).toStdString());

	    try {
            result = tableRPC.execute(strMethod, params);
			if (result.type() == array_type)
			{
				arr = result.get_array();
				this->offerAcceptTXID = QString::fromStdString(arr[0].get_str());
				this->offerAcceptGUID = QString::fromStdString(arr[1].get_str());
				if(this->offerAcceptGUID != QString("") && this->offerAcceptTXID != QString(""))
				{
					OfferPayDialog dlg(QString::fromStdString(stringFromVch(this->offer.vchRand)), this->offerAcceptGUID, this->offerAcceptTXID, this->notes,   this);
					if(dlg.exec())
					{
						this->offerPaid = dlg.getPaymentStatus();
						if(this->offerPaid)
						{
							this->offerAcceptGUID = QString("");
							this->offerAcceptTXID = QString("");
						}
						OfferAcceptDialog::accept();
					}
					else
					{
						OfferAcceptDialog::close();
					}
					return;

				}
			}
		}
		catch (Object& objError)
		{
			strError = find_value(objError, "message").get_str();
			QMessageBox::critical(this, windowTitle(),
			tr("Error accepting offer: \"%1\"").arg(QString::fromStdString(strError)),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		catch(std::exception& e)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("General exception when accepting offer"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
	
		if(this->offerAcceptGUID == QString("") || this->offerAcceptTXID == QString(""))
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Offer Accept returned empty result!"),
				QMessageBox::Ok, QMessageBox::Ok);
		}
   

}

bool OfferAcceptDialog::getPaymentStatus()
{
	return this->offerPaid;
}

