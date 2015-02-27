#include "acceptandpayofferlistpage.h"
#include "ui_acceptandpayofferlistpage.h"
#include "init.h"
#include "util.h"


#include "offer.h"

#include "bitcoingui.h"
#include "bitcoinrpc.h"

#include "guiutil.h"

#include <QSortFilterProxyModel>
#include <QClipboard>
#include <QMessageBox>
#include <QMenu>
#include <QString>
#include <QUrl>

using namespace std;
using namespace json_spirit;
extern const CRPCTable tableRPC;
extern string JSONRPCReply(const Value& result, const Value& error, const Value& id);

AcceptandPayOfferListPage::AcceptandPayOfferListPage(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AcceptandPayOfferListPage)
{
    ui->setupUi(this);
	this->offerAcceptGUID = string("");
	this->offerAcceptTXID = string("");
	this->offerPaid = false;
	
    ui->labelExplanation->setText(tr("Accept and purchase this offer, SysCoins will be used to complete the transaction."));
    connect(ui->acceptButton, SIGNAL(clicked()), this, SLOT(accept()));
	connect(ui->payButton, SIGNAL(clicked()), this, SLOT(pay()));
	connect(ui->lookupButton, SIGNAL(clicked()), this, SLOT(lookup()));
	connect(ui->offeridEdit, SIGNAL(textChanged(const QString &)), this, SLOT(resetState()));
}

AcceptandPayOfferListPage::~AcceptandPayOfferListPage()
{
    delete ui;
}
void AcceptandPayOfferListPage::resetState()
{
		this->offerAcceptGUID = string("");
		this->offerAcceptTXID = string("");
		this->offerPaid = false;
		updateCaption();
}
void AcceptandPayOfferListPage::updateCaption()
{
		
		if(this->offerPaid)
		{
			ui->labelExplanation->setText(tr("<font color='green'>You have successfully paid for this offer!</font>"));
		}
		else if(this->offerAcceptGUID != string(""))
		{
			ui->labelExplanation->setText(tr("<font color='red'>You have accepted the offer, please click Pay to complete your payment.</font>"));
		}
		else
		{
			ui->labelExplanation->setText(tr("Accept and purchase this offer, SysCoins will be used to complete the transaction."));
		}
		
}
// send offeraccept with offer guid/qty as params and then send offerpay with wtxid (first param of response) as param, using RPC commands.
void AcceptandPayOfferListPage::accept()
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
		string acceptTXID = string("");
		if(ui->qtyEdit->text().toInt() <= 0)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Invalid quantity when trying to accept offer!"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		if(this->offerAcceptTXID != string(""))
		{
			QMessageBox::critical(this, windowTitle(),
				tr("You have already accepted this offer, please click Pay after the Offer Accept (TxID %1) has confirmed on the blockchain!").arg(QString::fromStdString(this->offerAcceptTXID)),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		params.push_back(ui->offeridEdit->text().toStdString());
		params.push_back(ui->qtyEdit->text().toStdString());

	    try {
            result = tableRPC.execute(strMethod, params);
			if (result.type() == array_type)
			{
				arr = result.get_array();
				this->offerAcceptTXID = arr[0].get_str();
				this->offerAcceptGUID = arr[1].get_str();
				updateCaption();
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
	
		if(this->offerAcceptTXID != string(""))
		{
			QMessageBox::information(this, windowTitle(),
			tr("Offer accepted TxID %1").arg(QString::fromStdString(this->offerAcceptTXID)),
				QMessageBox::Ok, QMessageBox::Ok);	
		}
		else
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Offer Accept returned empty result!"),
				QMessageBox::Ok, QMessageBox::Ok);
		}
   

}
// send offeraccept with offer guid/qty as params and then send offerpay with wtxid (first param of response) as param, using RPC commands.
void AcceptandPayOfferListPage::pay()
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
		string payTXID = string("");
		if(ui->notesEdit->toPlainText() == QString(""))
		{
			QMessageBox::critical(this, windowTitle(),
				tr("You must enter a note to the seller, usually shipping address if the offer is for a physical item!"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		if(this->offerAcceptGUID == string(""))
		{
			QMessageBox::critical(this, windowTitle(),
				tr("You must accept the offer first!"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
	 
	  QMessageBox::StandardButton reply;
	  reply = QMessageBox::question(this, windowTitle(), tr("You are about to pay for this offer with your Syscoins. Would you like to proceed?"),
									QMessageBox::Yes|QMessageBox::No);
	  if (reply == QMessageBox::No) {
		return;
	  } 

		params.push_back(this->offerAcceptGUID);
		params.push_back(ui->notesEdit->toPlainText().toStdString());
	    try {
            result = tableRPC.execute(strMethod, params);
			if (result.type() == array_type)
			{
				arr = result.get_array();
				this->offerPaid = true;
				payTXID = arr[0].get_str();
				
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
			resetState();
			QMessageBox::information(this, windowTitle(),
			tr("Offer paid successfully! TxID %1").arg(QString::fromStdString(payTXID)),
				QMessageBox::Ok, QMessageBox::Ok);	
		}
		else
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Offer pay returned empty result!"),
				QMessageBox::Ok, QMessageBox::Ok);
		}
   

}
void AcceptandPayOfferListPage::lookup()
{

	string strError;
	string strMethod = string("offerinfo");
	Array params;
	Value result;
	params.push_back(ui->offeridEdit->text().toStdString());

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
			string descString = find_value(offerObj, "description").get_str();
			offerOut.sDescription = vchFromString(descString);
			Value outerDescValue;
			if (read_string(descString, outerDescValue))
			{
				if(outerDescValue.type() == obj_type)
				{
					const Object& outerDescObj = outerDescValue.get_obj();
					Value descValue = find_value(outerDescObj, "description");
					if (descValue.type() == str_type)
					{
						offerOut.sDescription = vchFromString(descValue.get_str());
					}
				}

			}
			offerOut.nFee = QString::number(find_value(offerObj, "service_fee").get_real()).toLongLong();
			offerOut.vchPaymentAddress = vchFromString(find_value(offerObj, "payment_address").get_str());
			setValue(offerOut);
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


}
bool AcceptandPayOfferListPage::handleURI(const QUrl &uri)
{
	  // return if URI is not valid or is no bitcoin URI
    if(!uri.isValid() || uri.scheme() != QString("syscoin"))
        return false;
	string strError;
	string strMethod = string("offerinfo");
	Array params;
	Value result;
	params.push_back(uri.path().toStdString());

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
			string descString = find_value(offerObj, "description").get_str();
			offerOut.sDescription = vchFromString(descString);
			Value outerDescValue;
			if (read_string(descString, outerDescValue))
			{
				if(outerDescValue.type() == obj_type)
				{
					const Object& outerDescObj = outerDescValue.get_obj();
					Value descValue = find_value(outerDescObj, "description");
					if (descValue.type() == str_type)
					{
						offerOut.sDescription = vchFromString(descValue.get_str());
					}
				}

			}
			offerOut.nFee = QString::number(find_value(offerObj, "service_fee").get_real()).toLongLong();
			offerOut.vchPaymentAddress = vchFromString(find_value(offerObj, "payment_address").get_str());
			setValue(offerOut);
		}
		 
		ui->notesEdit->setText(QString(""));
		ui->qtyEdit->setText(QString("0"));
	#if QT_VERSION < 0x050000
		QList<QPair<QString, QString> > items = uri.queryItems();
	#else
		QUrlQuery uriQuery(uri);
		QList<QPair<QString, QString> > items = uriQuery.queryItems();
	#endif
		for (QList<QPair<QString, QString> >::iterator i = items.begin(); i != items.end(); i++)
		{
			
			if (i->first == "quantity")
			{
				int64 qty = i->second.toLong();
				ui->qtyEdit->setText(QString::number(qty));
			}
			else if (i->first == "notes")
			{
				QString notes = i->second;
				ui->notesEdit->setText(notes);
			}
		}
	}
	catch (Object& objError)
	{
		string strError = find_value(objError, "message").get_str();
		QMessageBox::critical(this, windowTitle(),
		tr("Error in offer URI Handler: \"%1\"").arg(QString::fromStdString(strError)),
			QMessageBox::Ok, QMessageBox::Ok);
		return false;
	}
	catch(std::exception& e)
	{
		string strError = e.what();
		QMessageBox::critical(this, windowTitle(),
		tr("Exception in offer URI Handler: \"%1\"").arg(QString::fromStdString(strError)),
			QMessageBox::Ok, QMessageBox::Ok);
		return false;
	}

    return true;
}
void AcceptandPayOfferListPage::setValue(const COffer &offer)
{

    ui->offeridEdit->setText(QString::fromStdString(stringFromVch(offer.vchRand)));
	ui->infoTitle->setText(QString::fromStdString(stringFromVch(offer.sTitle)));
	ui->infoCategory->setText(QString::fromStdString(stringFromVch(offer.sCategory)));
	ui->infoPrice->setText(QString::number(offer.nPrice)+" SYS");
	ui->infoQty->setText(QString::number(offer.nQty));
	ui->infoDescription->setText(QString::fromStdString(stringFromVch(offer.sDescription)));
	ui->infoFee->setText(QString::number(offer.nFee));
	ui->infoPaymentAddress->setText(QString::fromStdString(stringFromVch(offer.vchPaymentAddress)));

}

bool AcceptandPayOfferListPage::handleURI(const QString& strURI)
{
	QString URI = strURI;
	if(URI.startsWith("syscoin://"))
    {
        URI.replace(0, 11, "syscoin:");
    }
    QUrl uriInstance(URI);
    return handleURI(uriInstance);
}

