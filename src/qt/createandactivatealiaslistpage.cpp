/*
 * Co-created by Sidhujag & Saffroncoin Developer - Roshan
 * Syscoin Developers 2014
 */
 
#include "createandactivatealiaslistpage.h"
#include "ui_createandactivatealiaslistpage.h"



#include "aliastablemodel.h"
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

CreateandActivateAliasListPage::CreateandActivateAliasListPage(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CreateandActivateAliasListPage),
    model(0),
    optionsModel(0)
{
    ui->setupUi(this);


    ui->labelExplanation->setText(tr("Create and Activate your Alias"));
     
    connect(ui->pushButton, SIGNAL(clicked()), this, SLOT(createandactivate()));
}

CreateandActivateAliasListPage::~CreateandActivateAliasListPage()
{
    delete ui;
}
// send aliasnew with alias name/value as params and then send aliasactivate with wtxid (first param of response)/rand as param, using RPC commands.
void CreateandActivateAliasListPage::createandactivate()
{
    if(ui->aliasName->text().trimmed().isEmpty())
    {
        ui->statusLabel->setStyleSheet("QLabel { color: red; }");
        ui->statusLabel->setText(QString("<nobr>") + tr("You haven't provided an Alias Name.") + QString("</nobr>"));
        return;
    }
    if(ui->aliasValue->text().trimmed().isEmpty())
    {
        ui->statusLabel->setStyleSheet("QLabel { color: red; }");
        ui->statusLabel->setText(QString("<nobr>") + tr("You haven't provided an Alias Value.") + QString("</nobr>"));
        return;
    }

        Array params;
        Value valError;
        Object ret ;
        Value valResult;
        Array arr;
        Value valId;
        Value result ;
        string strReply;
        string strError;
        string strMethod = string("aliasnew");
         
        params.push_back(ui->aliasName->text().toStdString());
        params.push_back(ui->aliasValue->text().toStdString());

        try {
            result = tableRPC.execute(strMethod, params);
        }
        catch (Object& objError)
        {
            strError = find_value(objError, "message").get_str();
            QMessageBox::critical(this, windowTitle(),
            tr("Error creating Alias: \"%1\"").arg(QString::fromStdString(strError)),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }
        catch(std::exception& e)
        {
            QMessageBox::critical(this, windowTitle(),
                tr("General exception when creating alias"),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }
    
        if (result.type() == array_type)
        {
            arr = result.get_array();
            strMethod = string("aliasactivate");
            params.clear();
            params.push_back(arr[0].get_str());

            try {
                result = tableRPC.execute(strMethod, params);
            }
            catch (Object& objError)
            {
                strError = find_value(objError, "message").get_str();
                QMessageBox::critical(this, windowTitle(),
                tr("Error activating Alias: \"%1\"").arg(QString::fromStdString(strError)),
                    QMessageBox::Ok, QMessageBox::Ok);
                return;
            }
            catch(std::exception& e)
            {
                QMessageBox::critical(this, windowTitle(),
                    tr("General exception when trying to activate alias"),
                    QMessageBox::Ok, QMessageBox::Ok);
                        return;
            }
            
        }
        else
        {
            QMessageBox::critical(this, windowTitle(),
                tr("Error: Invalid response from aliasactivate command"),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }

        QMessageBox::information(this, windowTitle(),
            tr("Alias is successfully created and activated!"),
                QMessageBox::Ok, QMessageBox::Ok);  

   

}
void CreateandActivateAliasListPage::setModel(AliasTableModel *model)
{
    return;
}

void CreateandActivateAliasListPage::setOptionsModel(OptionsModel *optionsModel)
{
    return;
}
bool CreateandActivateAliasListPage::handleURI(const QString& strURI)
{
 
    return false;
}
