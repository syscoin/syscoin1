#include "acceptandpayofferlistpage.h"
#include "ui_acceptandpayofferlistpage.h"



#include "offertablemodel.h"
#include "optionsmodel.h"
#include "bitcoingui.h"
#include "bitcoinrpc.h"

#include "guiutil.h"

#include <QSortFilterProxyModel>
#include <QClipboard>
#include <QMessageBox>
#include <QMenu>
#include <QString>
using namespace std;
using namespace json_spirit;
extern const CRPCTable tableRPC;
extern string JSONRPCReply(const Value& result, const Value& error, const Value& id);

AcceptandPayOfferListPage::AcceptandPayOfferListPage(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AcceptandPayOfferListPage),
    model(0),
    optionsModel(0)
{
    ui->setupUi(this);


    ui->labelExplanation->setText(tr("Accept and purchase this offer, SysCoins will be used to complete the transaction."));
	 
    connect(ui->pushButton, SIGNAL(clicked()), this, SLOT(acceptandpay()));
}

AcceptandPayOfferListPage::~AcceptandPayOfferListPage()
{
    delete ui;
}
// send offeraccept with offer guid/qty as params and then send offerpay with wtxid (first param of response) as param, using RPC commands.
void AcceptandPayOfferListPage::acceptandpay()
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
		 
		params.push_back(ui->offeridEdit->text().toStdString());
		params.push_back(ui->qtyEdit->text().toStdString());

	    try {
            result = tableRPC.execute(strMethod, params);
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
	
		if (result.type() == array_type)
		{
			arr = result.get_array();
			strMethod = string("offerpay");
			params.clear();
			params.push_back(arr[0].get_str());

		    try {
                result = tableRPC.execute(strMethod, params);
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
			
		}
		else
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Error: Invalid response from offeraccept command"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}

		QMessageBox::information(this, windowTitle(),
			tr("Offer transaction completed successfully!"),
				QMessageBox::Ok, QMessageBox::Ok);	

   

}
void AcceptandPayOfferListPage::setModel(OfferTableModel *model)
{
    return;
}

void AcceptandPayOfferListPage::setOptionsModel(OptionsModel *optionsModel)
{
    return;
}
bool AcceptandPayOfferListPage::handleURI(const QString& strURI)
{
 
    return false;
}