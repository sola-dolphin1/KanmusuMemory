#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "tweetdialog.h"
#include "settingsdialog.h"

#include <QDate>
#include <QTime>
#include <QString>
#include <QDir>
#include <QRgb>
#include <QMessageBox>


#include <QDebug>

#define URL_KANKORE "http://www.dmm.com/netgame/social/-/gadgets/=/app_id=854854/"
#define FLASH_POS_SEARCH_START_Y    40      //Flashの位置を探すときにY方向の開始座標（つまりDMMのヘッダを飛ばす）
#define KANKORE_WIDTH   800
#define KANKORE_HEIGHT  480

#define SETTING_FILE_NAME       "settings.ini"
#define SETTING_FILE_FORMAT     QSettings::IniFormat

#define STATUS_BAR_MSG_TIME     5000


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_savePath(""),
    m_finishCalibrated(false),
    m_settings(SETTING_FILE_NAME, SETTING_FILE_FORMAT, this)
{
    ui->setupUi(this);

    connect(ui->webView, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool)));
    connect(ui->webView, SIGNAL(loadProgress(int)), this, SLOT(loadProgress(int)));
    connect(ui->webView->page(), SIGNAL(scrollRequested(int,int,QRect)), this, SLOT(scrollRequested(int,int,QRect)));

    connect(this, SIGNAL(showMessage(QString)), ui->statusBar, SLOT(showMessage(QString)));
    connect(this, SIGNAL(showMessage(QString, int)), ui->statusBar, SLOT(showMessage(QString, int)));

    //設定読込み
    loadSettings();

    if(m_savePath.length() == 0){
        //設定を促す
        QMessageBox::information(this
                                            , tr("Information")
                                            , tr("Please select pictures save folder.")
                                            , QMessageBox::Ok
                                            , QMessageBox::Ok);
        m_savePath = SettingsDialog::selectSavePath(QDir::homePath());
//        m_savePath = QDir::homePath() + "/Pictures";
    }

    //設定
    QWebSettings::globalSettings()->setAttribute(QWebSettings::PluginsEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::JavascriptEnabled, true);
    //艦これ読込み
    ui->webView->load(QUrl(URL_KANKORE));

}

MainWindow::~MainWindow()
{
    //設定保存
    saveSettings();

    delete ui;
}



void MainWindow::resizeEvent(QResizeEvent *)
{
//#error "これが起きてキャリブレーション済みの設定が消える.からまぁなしで"
    //サイズが変わったらキャリブレーション解除
//    m_finishCalibrated = false;
}

//思い出を残す（ボタン）
void MainWindow::on_captureButton_clicked()
{
    captureGame();
}
//思い出を残す
void MainWindow::captureGame()
{
    qDebug() << "captureGame";

    if(!m_finishCalibrated){
        //キャリブレーションされてないので確認して実行
        if(QMessageBox::information(this
                                    , tr("Information")
                                    , tr("Please find Kankore position.")
                                    , QMessageBox::Ok
                                    , QMessageBox::NoButton)
                == QMessageBox::Ok){
            calibration();
        }
    }

    //キャリブレーションしてなければ実行しない
    if(!m_finishCalibrated){
        emit showMessage(QString(tr("cancel")), STATUS_BAR_MSG_TIME);
        return;
    }

    int x = m_flashPos.x() + m_scroolPos.x();// int((ui->webView->size().width()-16) / 2) - int(800 / 2);
    int y = m_flashPos.y() + m_scroolPos.y();
    QImage img(ui->webView->size(), QImage::Format_ARGB32);
    QImage img2(KANKORE_WIDTH, KANKORE_HEIGHT, QImage::Format_ARGB32);
    QPainter painter(&img);
    QPainter painter2(&img2);
    //全体を描画
    ui->webView->page()->view()->render(&painter);
    //2つ目へ必要な分だけコピー
    painter2.drawImage(QPoint(0,0), img, QRect(x, y, KANKORE_WIDTH, KANKORE_HEIGHT));
    QDate date(QDate::currentDate());
    QTime time(QTime::currentTime());
    QString path = m_savePath + "/kanmusu";
    path.append(QString("_%1-%2-%3_%4-%5-%6-%7")
            .arg(date.year()).arg(date.month()).arg(date.day())
            .arg(time.hour()).arg(time.minute()).arg(time.second()).arg(time.msec())
                );
    path.append(".png");
//    path.append(".jpg");
    qDebug() << "path:" << path;

    //保存する
    if(img2.save(path)){
        emit showMessage(QString(tr("save...")) + path, STATUS_BAR_MSG_TIME);


        //つぶやくダイアログ
        TweetDialog dlg(this);
        dlg.setImagePath(path);
        dlg.setToken(m_settings.value("token", "").toString());
        dlg.setTokenSecret(m_settings.value("tokenSecret", "").toString());
        dlg.user_id(m_settings.value("user_id", "").toString());
        dlg.screen_name(m_settings.value("screen_name", "").toString());
        dlg.exec();
        m_settings.setValue("token", dlg.token());
        m_settings.setValue("tokenSecret", dlg.tokenSecret());
        m_settings.setValue("user_id", dlg.user_id());
        m_settings.setValue("screen_name", dlg.screen_name());
//        m_settings.sync();

    }else{
        emit showMessage(QString(tr("fail")), STATUS_BAR_MSG_TIME);
    }

}
//キャリブレーション
void MainWindow::calibration()
{
    qDebug() << "calibration";
    int set_count = 0;
    QImage img(ui->webView->size(), QImage::Format_ARGB32);
    QPainter painter(&img);
    int w = ui->webView->size().width();
    int h = ui->webView->size().height();
    QRgb rgb;

    //全体を描画
    ui->webView->page()->view()->render(&painter);

    //横方向にはじっこを調べる
    for(int i=0; i<(w/2); i++){
        rgb = img.pixel(i, h/2);
        if(qGray(rgb) < 250){
            qDebug() << "found X:" << i << "," << (h/2) << "/" << qGray(rgb)
                     << "/" << qRed(rgb) << "," << qGreen(rgb) << "," << qBlue(rgb);
            m_flashPos.setX(i);
            set_count++;
            break;
        }
    }
    //縦方向に端っこを調べる
//    for(int i=h-1; i>=(h/2); i--){
//        rgb = img.pixel(w/2, i);
//        if(qGray(rgb) < 250){
//            qDebug() << "found Y:" << (w/2) << "," << (i-KANKORE_HEIGHT) << "/" << qGray(rgb)
//                     << "/" << qRed(rgb) << "," << qGreen(rgb) << "," << qBlue(rgb);
//            m_flashPos.setY(i - KANKORE_HEIGHT + 1 - m_scroolPos.y());
//            set_count++;
//            break;
//        }
//    }
    for(int i=FLASH_POS_SEARCH_START_Y; i<h; i++){
        rgb = img.pixel(w/2, i);
        if(qGray(rgb) < 250){
            qDebug() << "found Y:" << (w/2) << "," << i << "/" << qGray(rgb)
                     << "/" << qRed(rgb) << "," << qGreen(rgb) << "," << qBlue(rgb);
            m_flashPos.setY(i - m_scroolPos.y());
            set_count++;
            break;
        }
    }
    //キャリブレーション済み
    if(set_count == 2){
        emit showMessage(QString(tr("success calibration")), STATUS_BAR_MSG_TIME);
        m_finishCalibrated = true;
    }else{
        emit showMessage(QString(tr("fail calibration")), STATUS_BAR_MSG_TIME);
        m_finishCalibrated = false;
    }
}

//設定を読込み
void MainWindow::loadSettings()
{
    m_savePath = m_settings.value("path", "").toString();
    m_flashPos = m_settings.value("flashPos", QPoint(0, 0)).toPoint();
    m_finishCalibrated = m_settings.value("finishCalibrated", false).toBool();

}
//設定を保存
void MainWindow::saveSettings()
{
    m_settings.setValue("path", m_savePath);
    m_settings.setValue("flashPos", m_flashPos);
    m_settings.setValue("finishCalibrated", m_finishCalibrated);
//    m_settings.sync();
    qDebug() << "save settings";
}

//思い出を残す（メニュー）
void MainWindow::on_action_M_triggered()
{
    captureGame();
}
//思い出を表示
void MainWindow::on_action_L_triggered()
{

}

//再読み込み
void MainWindow::on_action_R_triggered()
{
    qDebug() << "reload";
    ui->webView->load(QUrl(URL_KANKORE));
//    ui->webView->reload();
}
//終了
void MainWindow::on_action_E_triggered()
{
    qDebug() << "exit";
    close();
}
//Flashの位置を探す
void MainWindow::on_actionFlash_C_triggered()
{
    calibration();
}

//WebViewの読込み完了
void MainWindow::loadFinished(bool ok)
{
    if(ok)
        emit showMessage(QString(tr("complete")));
    else
        emit showMessage(QString(tr("error")));
}
//WebViewの読込み状態
void MainWindow::loadProgress(int progress)
{
    emit showMessage(QString(tr("loading...%1%")).arg(progress));
}
//スクロール状態
void MainWindow::scrollRequested(int dx, int dy, const QRect &rectToScroll)
{
//    qDebug() << "scroll:" << dx << "," << dy << "/" << rectToScroll;
    m_scroolPos.setY(m_scroolPos.y() + dy);
//    if(m_scroolPos.y() < 0)
//        m_scroolPos.setY(0);

    qDebug() << "scroll:" << m_scroolPos.y();
}

//設定ダイアログ表示
void MainWindow::on_actionPreferences_triggered()
{
    SettingsDialog dlg(this);
    dlg.setSavePath(m_savePath);
    dlg.exec();
    if(dlg.result() != 0){
        //設定更新
        m_savePath = dlg.savePath();
    }
}