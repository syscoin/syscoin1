#include "acceptandpayofferlistpage.h"
#include "ui_acceptandpayofferlistpage.h"

#include "init.h"
#include "util.h"

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

extern Object JSONRPCExecOne(const Value& req);

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

void AcceptandPayOfferListPage::acceptandpay()
{

		Array params;
		Value valError;
		Object ret ;
		Value valResult;
		Array arr;
		Value valId;
		Object request;
		string strMethod = string("offeraccept");
		 
		params.push_back(ui->offeridEdit->text().toStdString());
		params.push_back(ui->qtyEdit->text().toStdString());
		request.push_back(Pair("method", strMethod));
		request.push_back(Pair("params", params));
		request.push_back(Pair("id", 1));	
	    try {
			ret = JSONRPCExecOne(request);
		}
		catch (Object& objError)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Error executing offeraccept: \"%1\"").arg(QString::fromStdString(find_value(objError, "message").get_str())),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		catch(...)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Unknown Error executing offeraccept"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		valError = find_value(ret, "error");
		if(valError.type() != null_type)
		{	
			QMessageBox::critical(this, windowTitle(),
				tr("Error accepting offer: \"%1\"").arg(QString::fromStdString(valError.get_str())),
                QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		else
		{
			valResult = find_value(ret, "result");
			if (valResult.type() == array_type)
			{
				arr = valResult.get_array();
				strMethod = string("offerpay");
				params.clear();
				request.clear();
				params.push_back(arr[0].get_str());
				request.push_back(Pair("method", strMethod));
				request.push_back(Pair("params", params));
				request.push_back(Pair("id", 1));	
			    try {
					ret = JSONRPCExecOne(request);
				}
				catch (Object& objError)
				{
					QMessageBox::critical(this, windowTitle(),
						tr("Error executing offerpay: \"%1\"").arg(QString::fromStdString(find_value(objError, "message").get_str())),
						QMessageBox::Ok, QMessageBox::Ok);
					return;
				}
				catch(...)
				{
					QMessageBox::critical(this, windowTitle(),
						tr("Unknown Error executing offerpay"),
						QMessageBox::Ok, QMessageBox::Ok);
					return;
				}
				ret = JSONRPCExecOne(request);
				valError = find_value(ret, "error");
				if(valError.type() != null_type)
				{	
					QMessageBox::critical(this, windowTitle(),
						tr("Error accepting offer: \"%1\"").arg(QString::fromStdString(valError.get_str())),
						QMessageBox::Ok, QMessageBox::Ok);
					return;
				}
				else

				{
					QMessageBox::information(this, windowTitle(),
						tr("Offer accepted and paid!"),
						QMessageBox::Ok, QMessageBox::Ok);
				}
			}
			else
			{
				QMessageBox::critical(this, windowTitle(),
					tr("Error: Invalid response from offeraccept command"),
					QMessageBox::Ok, QMessageBox::Ok);
				return;
			}

		}

   

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