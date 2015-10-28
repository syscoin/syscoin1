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
extern int64 convertCurrencyCodeToSyscoin(const vector<unsigned char> &vchCurrencyCode, const double &nPrice, const unsigned int &nHeight, int &precision);
extern int nBestHeight;
OfferAcceptDialog::OfferAcceptDialog(COffer* offer, QString quantity, QString notes, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OfferAcceptDialog), notes(notes), quantity(quantity)
{
    ui->setupUi(this);
	m_offer = new COffer();
	m_offer[0] = offer[0];
	int precision;
	int64 iPrice = convertCurrencyCodeToSyscoin(m_offer->sCurrencyCode, m_offer->GetPrice(), nBestHeight, precision);
	price = QString::number(ValueFromAmount(iPrice).get_real()*quantity.toUInt());

	ui->acceptMessage->setText(tr("There was a problem accepting this offer, please try again..."));

	ui->acceptMessage->setText(tr("Are you sure you want to purchase %1 of '%2'? You will be charged %3 SYS").arg(quantity).arg(QString::fromStdString(stringFromVch(offer->sTitle))).arg(price));
	
	this->offerPaid = false;
	connect(ui->acceptButton, SIGNAL(clicked()), this, SLOT(acceptOffer()));
}
void OfferAcceptDialog::on_cancelButton_clicked()
{
    reject();
}
OfferAcceptDialog::~OfferAcceptDialog()
{
    delete ui;
	delete m_offer;
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
  		CPubKey newDefaultKey;
		pwalletMain->GetKeyFromPool(newDefaultKey, false); 
		std::vector<unsigned char> vchPubKey(newDefaultKey.begin(), newDefaultKey.end());
		string strPubKey = HexStr(vchPubKey);

		string strMethod = string("offeraccept");
		if(this->quantity.toLong() <= 0)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Invalid quantity when trying to accept offer!"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		this->offerPaid = false;
		params.push_back(stringFromVch(m_offer->vchRand));
		params.push_back(strPubKey);
		params.push_back(this->quantity.toStdString());
		if(this->notes != QString(""))
		{
			params.push_back(this->notes.toStdString());
		}

	    try {
            result = tableRPC.execute(strMethod, params);
			if (result.type() != null_type)
			{
				string strResult = result.get_str();
				QString offerAcceptTXID = QString::fromStdString(strResult);
				if(offerAcceptTXID != QString(""))
				{
					OfferPayDialog dlg(QString::fromStdString(stringFromVch(m_offer->sTitle)), this->quantity, this->price, this);
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

