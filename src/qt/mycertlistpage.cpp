/*
 * Syscoin Developers 2015
 */
#include "mycertlistpage.h"
#include "ui_mycertlistpage.h"
#include "certtablemodel.h"
#include "clientmodel.h"
#include "optionsmodel.h"
#include "walletmodel.h"
#include "bitcoingui.h"
#include "editcertdialog.h"
#include "csvmodelwriter.h"
#include "guiutil.h"

#include <QSortFilterProxyModel>
#include <QClipboard>
#include <QMessageBox>
#include <QMenu>
MyCertListPage::MyCertListPage(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MyCertListPage),
    model(0),
    optionsModel(0)
{
    ui->setupUi(this);

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    ui->newCert->setIcon(QIcon());
    ui->copyCert->setIcon(QIcon());
    ui->exportButton->setIcon(QIcon());
#endif

	ui->buttonBox->setVisible(false);

    ui->labelExplanation->setText(tr("These are your registered Syscoin Certificates. Certificate operations (create, update, transfer) take 1 confirmation to appear in this table."));
	
	
    // Context menu actions
    QAction *copyCertAction = new QAction(ui->copyCert->text(), this);
    QAction *copyCertValueAction = new QAction(tr("&Copy Title"), this);
    QAction *editAction = new QAction(tr("&Edit"), this);
    QAction *transferCertAction = new QAction(tr("&Transfer Certificate"), this);

    // Build context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyCertAction);
    contextMenu->addAction(copyCertValueAction);
    contextMenu->addAction(editAction);
    contextMenu->addSeparator();
    contextMenu->addAction(transferCertAction);

    // Connect signals for context menu actions
    connect(copyCertAction, SIGNAL(triggered()), this, SLOT(on_copyCert_clicked()));
    connect(copyCertValueAction, SIGNAL(triggered()), this, SLOT(onCopyCertValueAction()));
    connect(editAction, SIGNAL(triggered()), this, SLOT(onEditAction()));
    connect(transferCertAction, SIGNAL(triggered()), this, SLOT(onTransferCertAction()));

    connect(ui->tableView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));

    // Pass through accept action from button box
    connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
}

MyCertListPage::~MyCertListPage()
{
    delete ui;
}
void MyCertListPage::showEvent ( QShowEvent * event )
{
    if(!walletModel) return;
    /*if(walletModel->getEncryptionStatus() == WalletModel::Locked)
	{
        ui->labelExplanation->setText(tr("<font color='red'>WARNING: Your wallet is currently locked. For security purposes you'll need to enter your passphrase in order to interact with Syscoin Certs. Because your wallet is locked you must manually refresh this table after creating or updating an Cert. </font> <a href=\"http://lockedwallet.syscoin.org\">more info</a><br><br>These are your registered Syscoin Certs. Cert updates take 1 confirmation to appear in this table."));
		ui->labelExplanation->setTextFormat(Qt::RichText);
		ui->labelExplanation->setTextInteractionFlags(Qt::TextBrowserInteraction);
		ui->labelExplanation->setOpenExternalLinks(true);
    }*/
}
void MyCertListPage::setModel(WalletModel *walletModel, CertTableModel *model)
{
    this->model = model;
	this->walletModel = walletModel;
    if(!model) return;
    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(model);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

  
		// Receive filter
		proxyModel->setFilterRole(CertTableModel::TypeRole);
		proxyModel->setFilterFixedString(CertTableModel::Cert);
       
    
		ui->tableView->setModel(proxyModel);
		ui->tableView->sortByColumn(0, Qt::AscendingOrder);
        ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    // Set column widths
#if QT_VERSION < 0x050000
    ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::Name, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::Title, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::ExpiresOn, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::ExpiresIn, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::Expired, QHeaderView::ResizeToContents);
#else
    ui->tableView->horizontalHeader()->setSectionResizeMode(CertTableModel::Name, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(CertTableModel::Title, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setSectionResizeMode(CertTableModel::ExpiresOn, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(CertTableModel::ExpiresIn, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(CertTableModel::Expired, QHeaderView::ResizeToContents);
#endif

    connect(ui->tableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
            this, SLOT(selectionChanged()));

    // Select row for newly created cert
    connect(model, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(selectNewCert(QModelIndex,int,int)));

    selectionChanged();
}

void MyCertListPage::setOptionsModel(ClientModel* clientmodel, OptionsModel *optionsModel)
{
    this->optionsModel = optionsModel;
	this->clientModel = clientmodel;
}

void MyCertListPage::on_copyCert_clicked()
{
    GUIUtil::copyEntryData(ui->tableView, CertTableModel::Name);
}

void MyCertListPage::onCopyCertValueAction()
{
    GUIUtil::copyEntryData(ui->tableView, CertTableModel::Title);
}

void MyCertListPage::onEditAction()
{
    if(!ui->tableView->selectionModel())
        return;
    QModelIndexList indexes = ui->tableView->selectionModel()->selectedRows();
    if(indexes.isEmpty())
        return;

    EditCertDialog dlg(EditCertDialog::EditCert);
    dlg.setModel(walletModel, model);
    QModelIndex origIndex = proxyModel->mapToSource(indexes.at(0));
    dlg.loadRow(origIndex.row());
    dlg.exec();
}

void MyCertListPage::onTransferCertAction()
{
    if(!ui->tableView->selectionModel())
        return;
    QModelIndexList indexes = ui->tableView->selectionModel()->selectedRows();
    if(indexes.isEmpty())
        return;

    EditCertDialog dlg(EditCertDialog::TransferCert);
    dlg.setModel(walletModel, model);
    QModelIndex origIndex = proxyModel->mapToSource(indexes.at(0));
    dlg.loadRow(origIndex.row());
    dlg.exec();
}
void MyCertListPage::on_refreshButton_clicked()
{
    if(!model)
        return;
    model->refreshCertTable();
}
void MyCertListPage::on_newCert_clicked()
{
    if(!model)
        return;

    EditCertDialog dlg(EditCertDialog::NewCert, this);
    dlg.setModel(walletModel,model);
    if(dlg.exec())
    {
        newCertToSelect = dlg.getCert();
    }
}
void MyCertListPage::selectionChanged()
{
    // Set button states based on selected tab and selection
    QTableView *table = ui->tableView;
    if(!table->selectionModel())
        return;

    if(table->selectionModel()->hasSelection())
    {
        ui->copyCert->setEnabled(true);
    }
    else
    {
        ui->copyCert->setEnabled(false);
    }
}

void MyCertListPage::done(int retval)
{
    QTableView *table = ui->tableView;
    if(!table->selectionModel() || !table->model())
        return;

    // Figure out which cert was selected, and return it
    QModelIndexList indexes = table->selectionModel()->selectedRows(CertTableModel::Name);

    foreach (QModelIndex index, indexes)
    {
        QVariant cert = table->model()->data(index);
        returnValue = cert.toString();
    }

    if(returnValue.isEmpty())
    {
        // If no cert entry selected, return rejected
        retval = Rejected;
    }

    QDialog::done(retval);
}

void MyCertListPage::on_exportButton_clicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Export Certificate Data"), QString(),
            tr("Comma separated file (*.csv)"));

    if (filename.isNull()) return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(proxyModel);
    writer.addColumn("Cert", CertTableModel::Name, Qt::EditRole);
    writer.addColumn("Title", CertTableModel::Title, Qt::EditRole);
	writer.addColumn("Expires On", CertTableModel::ExpiresOn, Qt::EditRole);
	writer.addColumn("Expires In", CertTableModel::ExpiresIn, Qt::EditRole);
	writer.addColumn("Expired", CertTableModel::Expired, Qt::EditRole);
    if(!writer.write())
    {
        QMessageBox::critical(this, tr("Error exporting"), tr("Could not write to file %1.").arg(filename),
                              QMessageBox::Abort, QMessageBox::Abort);
    }
}



void MyCertListPage::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if(index.isValid()) {
        contextMenu->exec(QCursor::pos());
    }
}

void MyCertListPage::selectNewCert(const QModelIndex &parent, int begin, int /*end*/)
{
    QModelIndex idx = proxyModel->mapFromSource(model->index(begin, CertTableModel::Name, parent));
    if(idx.isValid() && (idx.data(Qt::EditRole).toString() == newCertToSelect))
    {
        // Select row of newly created cert, once
        ui->tableView->setFocus();
        ui->tableView->selectRow(idx.row());
        newCertToSelect.clear();
    }
}
bool MyCertListPage::handleURI(const QString& strURI)
{
 
    return false;
}
