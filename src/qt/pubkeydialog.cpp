#include "pubkeydialog.h"
#include "ui_pubkeydialog.h"


#include "guiutil.h"

#include "bitcoingui.h"
#include "ui_interface.h"
#include "bitcoinrpc.h"
#include "script.h"
#include <QMessageBox>
#include <QClipboard>
#include "base58.h"
#include "init.cpp"
using namespace std;
using namespace json_spirit;

extern const CRPCTable tableRPC;
PubKeyDialog::PubKeyDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PubKeyDialog)
{
    ui->setupUi(this);

    
}

PubKeyDialog::~PubKeyDialog()
{
    delete ui;
}


void PubKeyDialog::accept()
{
    Array params;
    Value result ;
    string strReply;
    string strError;
    try {
		string strMethod = string("validateaddress");
		params.push_back(ui->addressEdit->text().toStdString());

        result = tableRPC.execute(strMethod, params);
		if (result.type() == obj_type)
		{
			Object& o = result.get_obj();
			string pubkey = "";
			const Value& key_value = find_value(o, "pubkey");
			if (key_value.type() == str_type)
			{
				pubkey = key_value.get_str();

				// Copy item (global clipboard)
				QApplication::clipboard()->setText(QString::fromStdString(pubkey), QClipboard::Clipboard);
				// Copy item (global mouse selection for e.g. X11 - NOP on Windows)
				QApplication::clipboard()->setText(QString::fromStdString(pubkey), QClipboard::Selection);
			}
			else
			{
				QMessageBox::critical(this, windowTitle(),
					tr("Error: could not read pubkey from validateaddress response"),
					QMessageBox::Ok, QMessageBox::Ok);
				return;
			}
		}
        else
        {
            QMessageBox::critical(this, windowTitle(),
                tr("Error: Invalid response from validateaddress command"),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }
    }
    catch (Object& objError)
    {
        strError = find_value(objError, "message").get_str();
        QMessageBox::critical(this, windowTitle(),
        tr("Error copying public key  \"%1\"").arg(QString::fromStdString(strError)),
            QMessageBox::Ok, QMessageBox::Ok);
        return;
    }
    catch(std::exception& e)
    {
        QMessageBox::critical(this, windowTitle(),
            tr("General exception when copying your public key"),
            QMessageBox::Ok, QMessageBox::Ok);
        return;
    }
	QMessageBox::information(this, windowTitle(),
		tr("The public key has been copied to your clipboard!"),
		QMessageBox::Ok, QMessageBox::Ok);
    QDialog::accept();
}


