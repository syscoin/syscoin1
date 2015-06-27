#include "editcertdialog.h"
#include "ui_editcertdialog.h"

#include "certtablemodel.h"
#include "guiutil.h"

#include <QDataWidgetMapper>
#include <QMessageBox>

EditCertDialog::EditCertDialog(Mode mode, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EditCertDialog), mapper(0), mode(mode), model(0)
{
    ui->setupUi(this);

    GUIUtil::setupAddressWidget(ui->certEdit, this);

    switch(mode)
    {
    case NewCert:
        setWindowTitle(tr("New certificate"));
        //ui->certEdit->setEnabled(false);
        break;
    case EditCert:
        setWindowTitle(tr("Edit certificate"));
        break;
    }

    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
}

EditCertDialog::~EditCertDialog()
{
    delete ui;
}

void EditCertDialog::setModel(CertTableModel *model)
{
    this->model = model;
    if(!model) return;

    mapper->setModel(model);
    mapper->addMapping(ui->labelEdit, CertTableModel::Title);
    mapper->addMapping(ui->certEdit, CertTableModel::Name);
}

void EditCertDialog::loadRow(int row)
{
    mapper->setCurrentIndex(row);
}

bool EditCertDialog::saveCurrentRow()
{
    if(!model) return false;

    switch(mode)
    {
    case NewCert:
        cert = model->addRow(
                CertTableModel::Cert,
                ui->labelEdit->text(),
                ui->certEdit->text(),
                ui->certEdit->text());
        break;
    case EditCert:
        if(mapper->submit())
        {
            cert = ui->certEdit->text();
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
                tr("The entered cert \"%1\" is not a valid Syscoin cert.").arg(ui->certEdit->text()),
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
