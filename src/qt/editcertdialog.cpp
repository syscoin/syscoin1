#include "editcertdialog.h"
#include "ui_editcertdialog.h"

#include "certtablemodel.h"
#include "guiutil.h"
#include "walletmodel.h"
#include "bitcoingui.h"
#include "ui_interface.h"
#include "bitcoinrpc.h"
#include "script.h"
#include <QDataWidgetMapper>
#include <QMessageBox>

using namespace std;
using namespace json_spirit;
extern int nBestHeight;
extern const CRPCTable tableRPC;
int64 GetCertNetworkFee(opcodetype seed, unsigned int nHeight);
EditCertDialog::EditCertDialog(Mode mode, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EditCertDialog), mapper(0), mode(mode), model(0)
{
    ui->setupUi(this);

    GUIUtil::setupAddressWidget(ui->nameEdit, this);
	ui->certLabel->setVisible(true);
	ui->certEdit->setVisible(true);
	ui->certEdit->setEnabled(false);
	ui->certDataEdit->setVisible(true);
	ui->certDataEdit->setEnabled(true);
	ui->certDataLabel->setVisible(true);
	ui->certDataEdit->setStyleSheet("color: rgb(0, 0, 0); background-color: rgb(255, 255, 255)");
	ui->privateLabel->setVisible(true);
	ui->privateBox->setVisible(true);
	ui->transferLabel->setVisible(false);
	ui->transferEdit->setVisible(false);
	ui->privateBox->addItem(tr("No"));
	ui->privateBox->addItem(tr("Yes"));
	
	ui->transferDisclaimer->setText(tr("<font color='red'>Enter the address of the recipient for public certificates and the public key of the recipient for private certificates. The recipient can find the public key from the <b>Copy Public Key</b> button in the <b>Certificates</b> tab. The recipient must communicate this key to you.</font>"));
    ui->transferDisclaimer->setVisible(false);
	switch(mode)
    {
    case NewCert:
		ui->certLabel->setVisible(false);
		ui->certEdit->setVisible(false);
        setWindowTitle(tr("New Cert"));
        break;
    case EditCert:
        setWindowTitle(tr("Edit Cert"));
        break;
    case TransferCert:
        setWindowTitle(tr("Transfer Cert"));
		ui->nameEdit->setEnabled(false);
		ui->certDataEdit->setVisible(false);
		ui->certDataEdit->setEnabled(false);
		ui->certDataLabel->setVisible(false);
		ui->privateLabel->setVisible(false);
		ui->privateBox->setVisible(false);
		ui->transferLabel->setVisible(true);
		ui->transferEdit->setVisible(true);
		ui->transferDisclaimer->setVisible(true);
        break;
    }
    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
	
}

EditCertDialog::~EditCertDialog()
{
    delete ui;
}

void EditCertDialog::setModel(WalletModel* walletModel, CertTableModel *model)
{
    this->model = model;
	this->walletModel = walletModel;
    if(!model) return;

    mapper->setModel(model);
	mapper->addMapping(ui->certEdit, CertTableModel::Name);
    mapper->addMapping(ui->nameEdit, CertTableModel::Title);
	mapper->addMapping(ui->certDataEdit, CertTableModel::Data);
    
}

void EditCertDialog::loadRow(int row, const QString &privatecert)
{
    mapper->setCurrentIndex(row);
	if(privatecert == tr("Yes"))
		ui->privateBox->setCurrentIndex(1);
	else
		ui->privateBox->setCurrentIndex(0);
}

bool EditCertDialog::saveCurrentRow()
{

    if(!model || !walletModel) return false;
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if(!ctx.isValid())
    {
		model->editStatus = CertTableModel::WALLET_UNLOCK_FAILURE;
        return false;
    }
	Array params;
	string strMethod;
	double updateFee,activateFee;
	std::string updateFeeStr,activateFeeStr;
	QMessageBox::StandardButton retval;
    switch(mode)
    {
    case NewCert:
        if (ui->nameEdit->text().trimmed().isEmpty()) {
            ui->nameEdit->setText("");
            QMessageBox::information(this, windowTitle(),
            tr("Empty name for Cert not allowed. Please try again"),
                QMessageBox::Ok, QMessageBox::Ok);
            return false;
        }
		activateFee = (double)GetCertNetworkFee(OP_CERT_ACTIVATE, nBestHeight)/(double)COIN;
		activateFeeStr = strprintf("%.2f", activateFee);
        retval = QMessageBox::question(this, tr("Confirm Certificate Activation"),
            tr("Warning: New certificate will cost ") + QString::fromStdString(activateFeeStr) + " SYS<br><br>" + tr("Are you sure you want to create this certificate?"),
				 QMessageBox::Yes|QMessageBox::Cancel,
				 QMessageBox::Cancel);
		if(retval != QMessageBox::Yes)
		{
			return false;
		}
		strMethod = string("certnew");
		
		params.push_back(ui->nameEdit->text().toStdString());
		params.push_back(ui->certDataEdit->toPlainText().toStdString());
		params.push_back(ui->privateBox->currentText() == QString("Yes")? "1": "0");
		try {
            Value result = tableRPC.execute(strMethod, params);
			if (result.type() != null_type)
			{
				string strResult = result.get_str();
				cert = ui->nameEdit->text();

				QMessageBox::information(this, windowTitle(),
                tr("New Certificate created successfully! TXID: \"%1\"").arg(QString::fromStdString(strResult)),
					QMessageBox::Ok, QMessageBox::Ok);
					
			}
		}
		catch (Object& objError)
		{
			string strError = find_value(objError, "message").get_str();
			QMessageBox::critical(this, windowTitle(),
			tr("Error creating new Cert: \"%1\"").arg(QString::fromStdString(strError)),
				QMessageBox::Ok, QMessageBox::Ok);
			break;
		}
		catch(std::exception& e)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("General exception creating new Cert"),
				QMessageBox::Ok, QMessageBox::Ok);
			break;
		}							

        break;
    case EditCert:
        if(mapper->submit())
        {
			updateFee = (double)GetCertNetworkFee(OP_CERT_UPDATE, nBestHeight)/(double)COIN;
			updateFeeStr = strprintf("%.2f", updateFee);
            retval = QMessageBox::question(this, tr("Confirm Certificate Update"),
                tr("Warning: Updating a certificate will cost ") + QString::fromStdString(updateFeeStr) + " SYS<br><br>" + tr("Are you sure you wish update this certificate?"),
					 QMessageBox::Yes|QMessageBox::Cancel,
					 QMessageBox::Cancel);
			if(retval != QMessageBox::Yes)
			{
				return false;
			}
			strMethod = string("certupdate");
			params.push_back(ui->certEdit->text().toStdString());
			params.push_back(ui->nameEdit->text().toStdString());
			params.push_back(ui->certDataEdit->toPlainText().toStdString());
			params.push_back(ui->privateBox->currentText() == QString("Yes")? "1": "0");
			
			try {
				Value result = tableRPC.execute(strMethod, params);
				if (result.type() != null_type)
				{
					string strResult = result.get_str();
					cert = ui->nameEdit->text() + ui->certEdit->text();

					QMessageBox::information(this, windowTitle(),
                    tr("Certificate updated successfully! TXID: \"%1\"").arg(QString::fromStdString(strResult)),
						QMessageBox::Ok, QMessageBox::Ok);
						
				}
			}
			catch (Object& objError)
			{
				string strError = find_value(objError, "message").get_str();
				QMessageBox::critical(this, windowTitle(),
				tr("Error updating Cert: \"%1\"").arg(QString::fromStdString(strError)),
					QMessageBox::Ok, QMessageBox::Ok);
				break;
			}
			catch(std::exception& e)
			{
				QMessageBox::critical(this, windowTitle(),
					tr("General exception updating Cert"),
					QMessageBox::Ok, QMessageBox::Ok);
				break;
			}	
        }
        break;
    case TransferCert:
        if(mapper->submit())
        {
			updateFee = (double)GetCertNetworkFee(OP_CERT_TRANSFER, nBestHeight)/(double)COIN;
			updateFeeStr = strprintf("%.2f", updateFee);
            retval = QMessageBox::question(this, tr("Confirm Certificate Transfer"),
                tr("Warning: Transferring a certificate will cost ") + QString::fromStdString(updateFeeStr) + " SYS<br><br>" + tr("Are you sure you wish transfer this certificate?"),
					 QMessageBox::Yes|QMessageBox::Cancel,
					 QMessageBox::Cancel);
			if(retval != QMessageBox::Yes)
			{
				return false;
			}
			strMethod = string("certtransfer");
			params.push_back(ui->certEdit->text().toStdString());
			params.push_back(ui->transferEdit->text().toStdString());
			try {
				Value result = tableRPC.execute(strMethod, params);
				if (result.type() != null_type)
				{
					string strResult = result.get_str();

					cert = ui->certEdit->text()+ui->transferEdit->text();

					QMessageBox::information(this, windowTitle(),
                    tr("Certificate transferred successfully! TXID: \"%1\"").arg(QString::fromStdString(strResult)),
						QMessageBox::Ok, QMessageBox::Ok);
						
				}
			}
			catch (Object& objError)
			{
				string strError = find_value(objError, "message").get_str();
				QMessageBox::critical(this, windowTitle(),
                tr("Error transferring Cert: \"%1\"").arg(QString::fromStdString(strError)),
					QMessageBox::Ok, QMessageBox::Ok);
				break;
			}
			catch(std::exception& e)
			{
				QMessageBox::critical(this, windowTitle(),
                    tr("General exception transferring Cert"),
					QMessageBox::Ok, QMessageBox::Ok);
				break;
			}	
        }
        break;
    }
    return !cert.isEmpty();
}

void EditCertDialog::accept()
{
    if(!model) return;

    if(!saveCurrentRow())
    {
        switch(model->getEditStatus())
        {
        case CertTableModel::OK:
            // Failed with unknown reason. Just reject.
            break;
        case CertTableModel::NO_CHANGES:
            // No changes were made during edit operation. Just reject.
            break;
        case CertTableModel::INVALID_CERT:
            QMessageBox::warning(this, windowTitle(),
                tr("The entered cert \"%1\" is not a valid Syscoin Cert.").arg(ui->certEdit->text()),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case CertTableModel::DUPLICATE_CERT:
            QMessageBox::warning(this, windowTitle(),
                tr("The entered cert \"%1\" is already taken.").arg(ui->certEdit->text()),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case CertTableModel::WALLET_UNLOCK_FAILURE:
            QMessageBox::critical(this, windowTitle(),
                tr("Could not unlock wallet."),
                QMessageBox::Ok, QMessageBox::Ok);
            break;

        }
        return;
    }
    QDialog::accept();
}

QString EditCertDialog::getCert() const
{
    return cert;
}

void EditCertDialog::setCert(const QString &cert)
{
    this->cert = cert;
    ui->certEdit->setText(cert);
}
