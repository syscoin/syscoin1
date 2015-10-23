/*
 * Syscoin Developers 2015
 */
#include "myacceptedofferlistpage.h"
#include "ui_myacceptedofferlistpage.h"
#include "offeraccepttablemodel.h"
#include "offeracceptinfodialog.h"
#include "clientmodel.h"
#include "optionsmodel.h"
#include "walletmodel.h"
#include "bitcoingui.h"
#include "bitcoinrpc.h"
#include "csvmodelwriter.h"
#include "guiutil.h"

using namespace std;
using namespace json_spirit;
extern const CRPCTable tableRPC;

#include <QSortFilterProxyModel>
#include <QClipboard>
#include <QMessageBox>
#include <QMenu>
MyAcceptedOfferListPage::MyAcceptedOfferListPage(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MyAcceptedOfferListPage),
    model(0),
    optionsModel(0)
{
    ui->setupUi(this);

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
	ui->refreshButton->setIcon(QIcon());
    ui->copyOffer->setIcon(QIcon());
    ui->exportButton->setIcon(QIcon());
#endif

	ui->buttonBox->setVisible(false);

    ui->labelExplanation->setText(tr("These are offers you have sold to others. Offer operations take 1 confirmation to appear in this table. Right click on an offer for more info including buyer message, quantity, date, etc."));
	
	connect(ui->tableView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(info()));	
    // Context menu actions
    QAction *copyOfferAction = new QAction(ui->copyOffer->text(), this);
    QAction *copyOfferValueAction = new QAction(tr("&Copy OfferAccept ID"), this);
	QAction *moreInfoAction = new QAction(tr("&More Info"), this);
	QAction *refundAction = new QAction(tr("&Refund"), this);

    // Build context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyOfferAction);
    contextMenu->addAction(copyOfferValueAction);
	contextMenu->addSeparator();
	contextMenu->addAction(moreInfoAction);
	contextMenu->addAction(refundAction);
    // Connect signals for context menu actions
    connect(copyOfferAction, SIGNAL(triggered()), this, SLOT(on_copyOffer_clicked()));
    connect(copyOfferValueAction, SIGNAL(triggered()), this, SLOT(onCopyOfferValueAction()));
	connect(moreInfoAction, SIGNAL(triggered()), this, SLOT(info()));
	connect(refundAction, SIGNAL(triggered()), this, SLOT(refund()));

    connect(ui->tableView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));


}

MyAcceptedOfferListPage::~MyAcceptedOfferListPage()
{
    delete ui;
}
void MyAcceptedOfferListPage::info()
{
    if(!ui->tableView->selectionModel())
        return;
    QModelIndexList selection = ui->tableView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        OfferAcceptInfoDialog dlg(selection.at(0));
        dlg.exec();
    }
}
void MyAcceptedOfferListPage::refund()
{
    if(!ui->tableView->selectionModel())
        return;
    QModelIndexList selection = ui->tableView->selectionModel()->selectedRows();
    if(selection.isEmpty())
    {
        return;
    }
	QString offerAcceptGUID = selection.at(0).data(OfferAcceptTableModel::GUIDRole).toString();

    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm refund"),
             tr("Warning: If this offer accept was resold you should ensure the reseller's wallet is online to process the refund to the buyer!") + "<br><br>" + tr("Coins will be returned to the buyer. Are you sure you wish to refund this offer accept?"),
             QMessageBox::Yes|QMessageBox::Cancel,
             QMessageBox::Cancel);
    if(retval == QMessageBox::Yes)
    {
		string strError;
		string strMethod = string("offerrefund");
		Array params;
		Value result;
		params.push_back(offerAcceptGUID.toStdString());

		try {
			result = tableRPC.execute(strMethod, params);

			if (result.type() == str_type)
			{
				QMessageBox::information(this, windowTitle(),
					tr("offer accept %1 refund was initiated successfully, please confirm with the buyer that he has recieved his funds after a few blocks!").arg(offerAcceptGUID),
					QMessageBox::Ok, QMessageBox::Ok);				
			}
			 

		}
		catch (Object& objError)
		{
			string strError = find_value(objError, "message").get_str();
			QMessageBox::critical(this, windowTitle(),
				tr("Could not refund this offer accept: %1").arg(QString::fromStdString(strError)),
					QMessageBox::Ok, QMessageBox::Ok);

		}
		catch(std::exception& e)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("There was an exception trying to refund this offer accept: ") + QString::fromStdString(e.what()),
					QMessageBox::Ok, QMessageBox::Ok);
		}

	}
}
void MyAcceptedOfferListPage::showEvent ( QShowEvent * event )
{
    if(!walletModel) return;
    /*if(walletModel->getEncryptionStatus() == WalletModel::Locked)
	{
        ui->labelExplanation->setText(tr("<font color='red'>WARNING: Your wallet is currently locked. For security purposes you'll need to enter your passphrase in order to interact with Syscoin Offers. Because your wallet is locked you must manually refresh this table after creating or updating an Offer. </font> <a href=\"http://lockedwallet.syscoin.org\">more info</a><br><br>These are your registered Syscoin Offers. Offer updates take 1 confirmation to appear in this table."));
		ui->labelExplanation->setTextFormat(Qt::RichText);
		ui->labelExplanation->setTextInteractionFlags(Qt::TextBrowserInteraction);
		ui->labelExplanation->setOpenExternalLinks(true);
    }*/
}
void MyAcceptedOfferListPage::setModel(WalletModel *walletModel, OfferAcceptTableModel *model)
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
		proxyModel->setFilterRole(OfferAcceptTableModel::TypeRole);
		proxyModel->setFilterFixedString(OfferAcceptTableModel::Offer);
       
    
		ui->tableView->setModel(proxyModel);
		ui->tableView->sortByColumn(0, Qt::AscendingOrder);
        ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    // Set column widths
#if QT_VERSION < 0x050000
    ui->tableView->horizontalHeader()->setResizeMode(OfferAcceptTableModel::Name, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setResizeMode(OfferAcceptTableModel::GUID, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setResizeMode(OfferAcceptTableModel::Title, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setResizeMode(OfferAcceptTableModel::Height, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setResizeMode(OfferAcceptTableModel::Price, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setResizeMode(OfferAcceptTableModel::Currency, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setResizeMode(OfferAcceptTableModel::Qty, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setResizeMode(OfferAcceptTableModel::Total, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setResizeMode(OfferAcceptTableModel::Status, QHeaderView::ResizeToContents);
#else
    ui->tableView->horizontalHeader()->setSectionResizeMode(OfferAcceptTableModel::Name, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(OfferAcceptTableModel::GUID, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setSectionResizeMode(OfferAcceptTableModel::Title, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setSectionResizeMode(OfferAcceptTableModel::Height, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(OfferAcceptTableModel::Price, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setSectionResizeMode(OfferAcceptTableModel::Currency, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setSectionResizeMode(OfferAcceptTableModel::Qty, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(OfferAcceptTableModel::Total, QHeaderView::ResizeToContents);
	 ui->tableView->horizontalHeader()->setSectionResizeMode(OfferAcceptTableModel::Status, QHeaderView::ResizeToContents);
#endif

    connect(ui->tableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
            this, SLOT(selectionChanged()));

    // Select row for newly created offer
    connect(model, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(selectNewOffer(QModelIndex,int,int)));

    selectionChanged();
}

void MyAcceptedOfferListPage::setOptionsModel(ClientModel* clientmodel, OptionsModel *optionsModel)
{
    this->optionsModel = optionsModel;
	this->clientModel = clientmodel;
}

void MyAcceptedOfferListPage::on_copyOffer_clicked()
{
    GUIUtil::copyEntryData(ui->tableView, OfferAcceptTableModel::Name);
}

void MyAcceptedOfferListPage::onCopyOfferValueAction()
{
    GUIUtil::copyEntryData(ui->tableView, OfferAcceptTableModel::GUID);
}


void MyAcceptedOfferListPage::on_refreshButton_clicked()
{
    if(!model)
        return;
    model->refreshOfferTable();
}

void MyAcceptedOfferListPage::selectionChanged()
{
    // Set button states based on selected tab and selection
    QTableView *table = ui->tableView;
    if(!table->selectionModel())
        return;

    if(table->selectionModel()->hasSelection())
    {
        ui->copyOffer->setEnabled(true);
    }
    else
    {
        ui->copyOffer->setEnabled(false);
    }
}

void MyAcceptedOfferListPage::done(int retval)
{
    QTableView *table = ui->tableView;
    if(!table->selectionModel() || !table->model())
        return;

    // Figure out which offer was selected, and return it
    QModelIndexList indexes = table->selectionModel()->selectedRows(OfferAcceptTableModel::Name);

    foreach (QModelIndex index, indexes)
    {
        QVariant offer = table->model()->data(index);
        returnValue = offer.toString();
    }

    if(returnValue.isEmpty())
    {
        // If no offer entry selected, return rejected
        retval = Rejected;
    }

    QDialog::done(retval);
}

void MyAcceptedOfferListPage::on_exportButton_clicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Export Offer Data"), QString(),
            tr("Comma separated file (*.csv)"));

    if (filename.isNull()) return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(proxyModel);
    writer.addColumn("Offer ID", OfferAcceptTableModel::Name, Qt::EditRole);
    writer.addColumn("OfferAccept ID", OfferAcceptTableModel::GUID, Qt::EditRole);
	writer.addColumn("Title", OfferAcceptTableModel::Title, Qt::EditRole);
	writer.addColumn("Height", OfferAcceptTableModel::Height, Qt::EditRole);
	writer.addColumn("Price", OfferAcceptTableModel::Price, Qt::EditRole);
	writer.addColumn("Currency", OfferAcceptTableModel::Currency, Qt::EditRole);
	writer.addColumn("Qty", OfferAcceptTableModel::Qty, Qt::EditRole);
	writer.addColumn("Total", OfferAcceptTableModel::Total, Qt::EditRole);
	writer.addColumn("Status", OfferAcceptTableModel::Status, Qt::EditRole);
    if(!writer.write())
    {
        QMessageBox::critical(this, tr("Error exporting"), tr("Could not write to file %1.").arg(filename),
                              QMessageBox::Abort, QMessageBox::Abort);
    }
}



void MyAcceptedOfferListPage::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if(index.isValid()) {
        contextMenu->exec(QCursor::pos());
    }
}

void MyAcceptedOfferListPage::selectNewOffer(const QModelIndex &parent, int begin, int /*end*/)
{
    QModelIndex idx = proxyModel->mapFromSource(model->index(begin, OfferAcceptTableModel::Name, parent));
    if(idx.isValid() && (idx.data(Qt::EditRole).toString() == newOfferToSelect))
    {
        // Select row of newly created offer, once
        ui->tableView->setFocus();
        ui->tableView->selectRow(idx.row());
        newOfferToSelect.clear();
    }
}
bool MyAcceptedOfferListPage::handleURI(const QString& strURI)
{
 
    return false;
}
