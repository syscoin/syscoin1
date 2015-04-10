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

OfferAcceptDialog::OfferAcceptDialog(QString title, QString price, QString quantity, QString offerAcceptGUID, QString notes, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OfferAcceptDialog), offerAcceptGUID(offerAcceptGUID), notes(notes), quantity(quantity), title(title), price(price)
{
    ui->setupUi(this);
	ui->acceptMessage->setText(tr("There was a problem accepting this offer, please try again..."));

	ui->acceptMessage->setText(tr("Are you sure you want to purchase %1 of '%2'? You will be charged %3 SYS").arg(quantity).arg(title).arg(price));
	
	this->offerPaid = false;
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
		if(this->quantity.toLong() <= 0)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Invalid quantity when trying to accept offer!"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		this->offerPaid = false;
		params.push_back(this->offerAcceptGUID.toStdString());
		params.push_back(this->quantity.toStdString());
		if(this->notes != QString(""))
		{
			params.push_back(this->notes.toStdString());
		}

	    try {
            result = tableRPC.execute(strMethod, params);
			if (result.type() == array_type)
			{
				arr = result.get_array();
				QString offerAcceptTXID = QString::fromStdString(arr[0].get_str());
				if(offerAcceptTXID != QString(""))
				{
					OfferPayDialog dlg(this->title, this->quantity, this->price, this);
					dlg.exec();
					this->offerPaid = true;
					OfferAcceptDialog::accept();
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
	
   

}

bool OfferAcceptDialog::getPaymentStatus()
{
	return this->offerPaid;
}

