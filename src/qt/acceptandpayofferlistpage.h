#ifndef ACCEPTANDPAYOFFERLISTPAGE_H
#define ACCEPTANDPAYOFFERLISTPAGE_H

#include <QDialog>

namespace Ui {
    class AcceptandPayOfferListPage;
}
class JSONRequest;

class OptionsModel;
class COffer;
QT_BEGIN_NAMESPACE
class QTableView;
class QItemSelection;
class QSortFilterProxyModel;
class QMenu;
class QModelIndex;
class QUrl;
class QNetworkAccessManager;
class QPixmap;
QT_END_NAMESPACE

/** Widget that shows a list of owned offeres.
  */
class AcceptandPayOfferListPage : public QDialog
{
    Q_OBJECT

public:


    explicit AcceptandPayOfferListPage(QWidget *parent = 0);
    ~AcceptandPayOfferListPage();

    const QString &getReturnValue() const { return returnValue; }
	bool handleURI(const QUrl &uri);
	bool handleURI(const QString& strURI);
	void setValue(COffer &offer);
	void updateCaption();
	void OpenPayDialog();
public slots:
    void acceptOffer();
	bool lookup(QString id = QString(""));
	void resetState();
	void netwManagerFinished();

private:
    Ui::AcceptandPayOfferListPage *ui;
	bool URIHandled;
    QString returnValue;
	bool offerPaid;
    QMenu *contextMenu;
    QAction *deleteAction; // to be able to explicitly disable it
	QNetworkAccessManager* m_netwManager;
	QPixmap m_placeholderImage;
	
};

#endif // ACCEPTANDPAYOFFERLISTPAGE_H
