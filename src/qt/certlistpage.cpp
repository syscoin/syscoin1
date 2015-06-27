#include "certlistpage.h"
#include "ui_certlistpage.h"

#include "certtablemodel.h"
#include "optionsmodel.h"
#include "bitcoingui.h"
#include "editcertdialog.h"
#include "csvmodelwriter.h"
#include "guiutil.h"

#include <QSortFilterProxyModel>
#include <QClipboard>
#include <QMessageBox>
#include <QMenu>

CertListPage::CertListPage(Mode mode, Tabs tab, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CertListPage),
    model(0),
    optionsModel(0),
    mode(mode),
    tab(tab)
{
    ui->setupUi(this);

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    ui->newCert->setIcon(QIcon());
    ui->copyCert->setIcon(QIcon());
    ui->exportButton->setIcon(QIcon());
#endif

    switch(mode)
    {
    case ForTransferring:
        connect(ui->tableView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(accept()));
        ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->tableView->setFocus();
        ui->exportButton->hide();
        break;
    case ForEditing:
        ui->buttonBox->setVisible(false);
        break;
    }
    switch(tab)
    {
    case CertTab:
        ui->labelExplanation->setText(tr("These are your registered Syscoin certs. Remember to check the expiration depth of your certs regularly."));
        break;
    }

    // Context menu actions
    QAction *copyCertAction = new QAction(ui->copyCert->text(), this);
    QAction *copyCertValueAction = new QAction(tr("Copy Va&lue"), this);
    QAction *editAction = new QAction(tr("&Edit"), this);
    QAction *transferCertAction = new QAction(tr("&Transfer Cert"), this);

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


    connect(ui->tableView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));

    // Pass through accept action from button box
    connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
}

CertListPage::~CertListPage()
{
    delete ui;
}

void CertListPage::setModel(CertTableModel *model)
{
    this->model = model;
    if(!model) return;

    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(model);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    switch(tab)
    {
    case CertTab:
        // Receive filter
        proxyModel->setFilterRole(CertTableModel::TypeRole);
        proxyModel->setFilterFixedString(CertTableModel::Cert);
        break;
    }
    ui->tableView->setModel(proxyModel);
    ui->tableView->sortByColumn(0, Qt::AscendingOrder);

    // Set column widths
#if QT_VERSION < 0x050000
    ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::Name, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::Title, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::ExpirationDepth, QHeaderView::ResizeToContents);
#else
    ui->tableView->horizontalHeader()->setSectionResizeMode(CertTableModel::Name, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(CertTableModel::Title, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setSectionResizeMode(CertTableModel::ExpirationDepth, QHeaderView::ResizeToContents);
#endif

    connect(ui->tableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
            this, SLOT(selectionChanged()));

    // Select row for newly created cert
    connect(model, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(selectNewCert(QModelIndex,int,int)));

    selectionChanged();
}

void CertListPage::setOptionsModel(OptionsModel *optionsModel)
{
    this->optionsModel = optionsModel;
}

void CertListPage::on_copyCert_clicked()
{
    GUIUtil::copyEntryData(ui->tableView, CertTableModel::Name);
}

void CertListPage::onCopyCertValueAction()
{
    GUIUtil::copyEntryData(ui->tableView, CertTableModel::Title);
}

void CertListPage::onEditAction()
{
    if(!ui->tableView->selectionModel())
        return;
    QModelIndexList indexes = ui->tableView->selectionModel()->selectedRows();
    if(indexes.isEmpty())
        return;

    EditCertDialog dlg(
            tab == CertTab ?
            EditCertDialog::EditCert :
            EditCertDialog::EditCert);
    dlg.setModel(model);
    QModelIndex origIndex = proxyModel->mapToSource(indexes.at(0));
    dlg.loadRow(origIndex.row());
    dlg.exec();
}

void CertListPage::onTransferCertAction()
{
    QTableView *table = ui->tableView;
    QModelIndexList indexes = table->selectionModel()->selectedRows(CertTableModel::Name);

    foreach (QModelIndex index, indexes)
    {
        QString cert = index.data().toString();
        emit transferCert(cert);
    }
}

void CertListPage::on_newCert_clicked()
{
    if(!model)
        return;

    EditCertDialog dlg(
            tab == CertTab ?
            EditCertDialog::NewCert :
            EditCertDialog::NewCert, this);
    dlg.setModel(model);
    if(dlg.exec())
    {
        newCertToSelect = dlg.getCert();
    }
}

void CertListPage::selectionChanged()
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

void CertListPage::done(int retval)
{
    QTableView *table = ui->tableView;
    if(!table->selectionModel() || !table->model())
        return;
    // When this is a tab/widget and not a model dialog, ignore "done"
    if(mode == ForEditing)
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

void CertListPage::on_exportButton_clicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Export Cert Data"), QString(),
            tr("Comma separated file (*.csv)"));

    if (filename.isNull()) return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(proxyModel);
    writer.addColumn("Key", CertTableModel::Name, Qt::EditRole);
    writer.addColumn("Title", CertTableModel::Title, Qt::EditRole);
    writer.addColumn("Expiration Depth", CertTableModel::ExpirationDepth, Qt::EditRole);
    if(!writer.write())
    {
        QMessageBox::critical(this, tr("Error exporting"), tr("Could not write to file %1.").arg(filename),
                              QMessageBox::Abort, QMessageBox::Abort);
    }
}



void CertListPage::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if(index.isValid()) {
        contextMenu->exec(QCursor::pos());
    }
}

void CertListPage::selectNewCert(const QModelIndex &parent, int begin, int /*end*/)
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
