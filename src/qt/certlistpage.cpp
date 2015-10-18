#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "certlistpage.h"
#include "ui_certlistpage.h"

#include "certtablemodel.h"
#include "optionsmodel.h"
#include "walletmodel.h"
#include "bitcoingui.h"
#include "bitcoinrpc.h"
#include "editcertdialog.h"
#include "csvmodelwriter.h"
#include "guiutil.h"
#include "ui_interface.h"
#include <QSortFilterProxyModel>
#include <QClipboard>
#include <QMessageBox>
#include <QKeyEvent>
#include <QMenu>
#include "main.h"
using namespace std;
using namespace json_spirit;

extern const CRPCTable tableRPC;
extern string JSONRPCReply(const Value& result, const Value& error, const Value& id);
int GetCertDisplayExpirationDepth();
CertListPage::CertListPage(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CertListPage),
    model(0),
    optionsModel(0)
{
    ui->setupUi(this);

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    ui->copyCert->setIcon(QIcon());
    ui->exportButton->setIcon(QIcon());
#endif

    ui->labelExplanation->setText(tr("Search for Syscoin Certificates"));
	
    // Context menu actions
    QAction *copyCertAction = new QAction(ui->copyCert->text(), this);
    QAction *copyCertValueAction = new QAction(tr("&Copy Value"), this);


    // Build context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyCertAction);
    contextMenu->addAction(copyCertValueAction);

    // Connect signals for context menu actions
    connect(copyCertAction, SIGNAL(triggered()), this, SLOT(on_copyCert_clicked()));
    connect(copyCertValueAction, SIGNAL(triggered()), this, SLOT(onCopyCertValueAction()));
   
    connect(ui->tableView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));


	ui->lineEditCertSearch->setPlaceholderText(tr("Enter search term, regex accepted (ie: ^name returns all Certificates starting with 'name')"));
}

CertListPage::~CertListPage()
{
    delete ui;
}
void CertListPage::showEvent ( QShowEvent * event )
{
    if(!walletModel) return;
    /*if(walletModel->getEncryptionStatus() == WalletModel::Locked)
	{
        ui->labelExplanation->setText(tr("<font color='red'>WARNING: Your wallet is currently locked. For security purposes you'll need to enter your passphrase in order to search Syscoin Certs.</font> <a href=\"http://lockedwallet.syscoin.org\">more info</a>"));
		ui->labelExplanation->setTextFormat(Qt::RichText);
		ui->labelExplanation->setTextInteractionFlags(Qt::TextBrowserInteraction);
		ui->labelExplanation->setOpenExternalLinks(true);
    }*/
}
void CertListPage::setModel(WalletModel* walletModel, CertTableModel *model)
{
    this->model = model;
	this->walletModel = walletModel;
    if(!model) return;

    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(model);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterRole(CertTableModel::TypeRole);
    proxyModel->setFilterFixedString(CertTableModel::Cert);
    ui->tableView->setModel(proxyModel);
    ui->tableView->sortByColumn(0, Qt::AscendingOrder);

    // Set column widths
#if QT_VERSION < 0x050000
    ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::Name, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::Title, QHeaderView::Stretch);
	ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::Data, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::ExpiresOn, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::ExpiresIn, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setResizeMode(CertTableModel::Expired, QHeaderView::ResizeToContents);
#else
    ui->tableView->horizontalHeader()->setSectionResizeMode(CertTableModel::Name, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(CertTableModel::Title, QHeaderView::Stretch);
	ui->tableView->horizontalHeader()->setSectionResizeMode(CertTableModel::Data, QHeaderView::Stretch);
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
void CertListPage::keyPressEvent(QKeyEvent * event)
{
  if( event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter )
  {
	on_searchCert_clicked();
    event->accept();
  }
  else
    QDialog::keyPressEvent( event );
}
void CertListPage::on_exportButton_clicked()
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
    writer.addColumn("Certificate", CertTableModel::Name, Qt::EditRole);
    writer.addColumn("Title", CertTableModel::Title, Qt::EditRole);
	writer.addColumn("Data", CertTableModel::Data, Qt::EditRole);
	writer.addColumn("Expires On", CertTableModel::ExpiresOn, Qt::EditRole);
	writer.addColumn("Expires In", CertTableModel::ExpiresIn, Qt::EditRole);
	writer.addColumn("Expired", CertTableModel::Expired, Qt::EditRole);
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

void CertListPage::on_searchCert_clicked()
{
    if(!walletModel) return;
    if(ui->lineEditCertSearch->text().trimmed().isEmpty())
    {
        QMessageBox::warning(this, tr("Error Searching Certificates"),
            tr("Please enter search term"),
            QMessageBox::Ok, QMessageBox::Ok);
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
        string strMethod = string("certfilter");
		string name_str;
		string value_str;
		string data_str;
		string expires_in_str;
		string expires_on_str;
		string expired_str;
		int expired = 0;
		int expires_in = 0;
		int expires_on = 0; 
        params.push_back(ui->lineEditCertSearch->text().toStdString());
        params.push_back(GetCertDisplayExpirationDepth());

        try {
            result = tableRPC.execute(strMethod, params);
        }
        catch (Object& objError)
        {
            strError = find_value(objError, "message").get_str();
            QMessageBox::critical(this, windowTitle(),
            tr("Error searching Certificate: \"%1\"").arg(QString::fromStdString(strError)),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }
        catch(std::exception& e)
        {
            QMessageBox::critical(this, windowTitle(),
                tr("General exception when searching certficiates"),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }
		if (result.type() == array_type)
			{
				this->model->clear();
			
			  Array arr = result.get_array();
			  BOOST_FOREACH(Value& input, arr)
				{
				if (input.type() != obj_type)
					continue;
				Object& o = input.get_obj();
				name_str = "";
				value_str = "";
				data_str = "";
				expires_in_str = "";
				expires_on_str = "";
				expired = 0;
				expires_in = 0;
				expires_on = 0;

					const Value& name_value = find_value(o, "cert");
					if (name_value.type() == str_type)
						name_str = name_value.get_str();
					const Value& value_value = find_value(o, "title");
					if (value_value.type() == str_type)
						value_str = value_value.get_str();
					const Value& data_value = find_value(o, "data");
					if (data_value.type() == str_type)
						data_str = data_value.get_str();
					const Value& expires_on_value = find_value(o, "expires_on");
					if (expires_on_value.type() == int_type)
						expires_on = expires_on_value.get_int();
					const Value& expires_in_value = find_value(o, "expires_in");
					if (expires_in_value.type() == int_type)
						expires_in = expires_in_value.get_int();
					const Value& expired_value = find_value(o, "expired");
					if (expired_value.type() == int_type)
						expired = expired_value.get_int();
				if(expired == 1)
				{
					expired_str = "Expired";
				}
				else
				{
					expired_str = "Valid";
					expires_in_str = strprintf("%d Blocks", expires_in);
					expires_on_str = strprintf("Block %d", expires_on);
				}

				
				
				model->addRow(CertTableModel::Cert,
						QString::fromStdString(name_str),
						QString::fromStdString(value_str),
						QString::fromStdString(data_str),
						QString::fromStdString(expires_on_str),
						QString::fromStdString(expires_in_str),
						QString::fromStdString(expired_str));
					this->model->updateEntry(QString::fromStdString(name_str),
						QString::fromStdString(value_str),
						QString::fromStdString(data_str),
						QString::fromStdString(expires_on_str),
						QString::fromStdString(expires_in_str),
						QString::fromStdString(expired_str), AllCert, CT_NEW);	
			  }

            
         }   
        else
        {
            QMessageBox::critical(this, windowTitle(),
                tr("Error: Invalid response from certfilter command"),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }

}

bool CertListPage::handleURI(const QString& strURI)
{
 
    return false;
}
