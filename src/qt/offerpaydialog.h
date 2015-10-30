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
    explicit OfferPayDialog(QString title, QString quantity, QString price, QWidget *parent = 0);
    ~OfferPayDialog();


private:
	Ui::OfferPayDialog *ui;
 
};

#endif // OFFERPAYDIALOG_H
