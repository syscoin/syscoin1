#ifndef OFFERPAYDIALOG_H
#define OFFERPAYDIALOG_H

#include <QDialog>
namespace Ui {
    class OfferPayDialog;
}

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

class OfferPayDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OfferPayDialog(QString offerID, QString offerAcceptGUID, QString offerAcceptTXID, QString notes, QWidget *parent = 0);
    ~OfferPayDialog();


    bool getPaymentStatus();
	bool lookup();

private:
	Ui::OfferPayDialog *ui;
	QTimer* timer;
	int progress;
	QString offerID;
	QString offerAcceptTXID;
	QString offerAcceptGUID;
	QString notes;
	bool offerPaid;  
private slots:
	void offerAcceptWatcher();
    void pay();

};

#endif // OFFERPAYDIALOG_H
