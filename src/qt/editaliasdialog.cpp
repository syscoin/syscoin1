#include "editaliasdialog.h"
#include "ui_editaliasdialog.h"

#include "aliastablemodel.h"
#include "guiutil.h"

#include <QDataWidgetMapper>
#include <QMessageBox>

EditAliasDialog::EditAliasDialog(Mode mode, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EditAliasDialog), mapper(0), mode(mode), model(0)
{
    ui->setupUi(this);

    GUIUtil::setupAddressWidget(ui->aliasEdit, this);

    switch(mode)
    {
    case NewDataAlias:
        setWindowTitle(tr("New data alias"));
        //ui->aliasEdit->setEnabled(false);
        break;
    case NewAlias:
        setWindowTitle(tr("New alias"));
        break;
    case EditDataAlias:
        setWindowTitle(tr("Edit data alias"));
        break;
    case EditAlias:
        setWindowTitle(tr("Edit alias"));
        break;
    }

    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
}

EditAliasDialog::~EditAliasDialog()
{
    delete ui;
}

void EditAliasDialog::setModel(AliasTableModel *model)
{
    this->model = model;
    if(!model) return;

    mapper->setModel(model);
    mapper->addMapping(ui->labelEdit, AliasTableModel::Value);
    mapper->addMapping(ui->aliasEdit, AliasTableModel::Name);
}

void EditAliasDialog::loadRow(int row)
{
    mapper->setCurrentIndex(row);
}

bool EditAliasDialog::saveCurrentRow()
{
    if(!model) return false;

    switch(mode)
    {
    case NewDataAlias:
    case NewAlias:
        alias = model->addRow(
                mode == NewAlias ? AliasTableModel::Alias : AliasTableModel::DataAlias,
                ui->labelEdit->text(),
                ui->aliasEdit->text());
        break;
    case EditDataAlias:
    case EditAlias:
        if(mapper->submit())
        {
            alias = ui->aliasEdit->text();
        }
        break;
    }
    return !alias.isEmpty();
}

void EditAliasDialog::accept()
{
    if(!model) return;

    if(!saveCurrentRow())
    {
        switch(model->getEditStatus())
        {
        case AliasTableModel::OK:
            // Failed with unknown reason. Just reject.
            break;
        case AliasTableModel::NO_CHANGES:
            // No changes were made during edit operation. Just reject.
            break;
        case AliasTableModel::INVALID_ALIAS:
            QMessageBox::warning(this, windowTitle(),
                tr("The entered alias \"%1\" is not a valid SysCoin alias.").arg(ui->aliasEdit->text()),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case AliasTableModel::DUPLICATE_ALIAS:
            QMessageBox::warning(this, windowTitle(),
                tr("The entered alias \"%1\" is already taken.").arg(ui->aliasEdit->text()),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case AliasTableModel::WALLET_UNLOCK_FAILURE:
            QMessageBox::critical(this, windowTitle(),
                tr("Could not unlock wallet."),
                QMessageBox::Ok, QMessageBox::Ok);
            break;

        }
        return;
    }
    QDialog::accept();
}

QString EditAliasDialog::getAlias() const
{
    return alias;
}

void EditAliasDialog::setAlias(const QString &alias)
{
    this->alias = alias;
    ui->aliasEdit->setText(alias);
}
