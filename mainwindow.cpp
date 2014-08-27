#include "mainwindow.h"
#include "ui_mainwindow.h"


#define DATA_SIZE 4


MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent),
  ui(new Ui::MainWindow)
{
  ui->setupUi(this);

  serial = new QSerialPort(this);
  connect(serial, SIGNAL(readyRead()), this, SLOT(readData()));

  fetch_button = new QPushButtonProgress(this, "Dump memory");
  ui->button_spot->addWidget(fetch_button);
  connect(fetch_button, SIGNAL(clicked()), this, SLOT(fetch_memory()));

  model = new QStandardItemModel(0,0,this);
  ui->table->setModel(model);

  newDataRead = 0;

  timer = new QTimer(this);
  connect(timer, SIGNAL(timeout()), this, SLOT(updateData()));
  timer->start(1000);

  QTimer *timer2 = new QTimer(this);
  connect(timer2, SIGNAL(timeout()), this, SLOT(updateRaw()));
  timer2->start(1000);

  decodeProgressBar = new QProgressBar();
  decodeData = new QLabel();
  decodeData->setText("Decoded data");
  ui->horizontalLayoutDecodeProgress->addWidget(decodeData);
  ui->horizontalLayoutDecodeProgress->addWidget(decodeProgressBar);
  decodeProgressBar->hide();


  serialPortList = new QComboBox(this);
  listAvailablePorts = QSerialPortInfo::availablePorts();
  serialPortList->addItem("");
  for(int i=0; i<listAvailablePorts.size(); i++) serialPortList->addItem(listAvailablePorts.at(i).portName());
  serialPortList->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
  ui->button_spot->addWidget(serialPortList);
  connect(serialPortList, SIGNAL(currentIndexChanged(int)), this, SLOT(configureSerialConnexion()));
}

MainWindow::~MainWindow()
{
  delete ui;
}



void MainWindow::configureSerialConnexion(){

  serial->close();

  int currentIndex = serialPortList->currentIndex();
  if(currentIndex < 1){ //if we selected the blank value we quit

      return;
  }

  serial->setPort(listAvailablePorts.at(currentIndex-1)); //-1 cause of the first blank value in the list
  serial->setBaudRate(115200);    //115200/230400/921600
  serial->setDataBits(QSerialPort::Data8);
  serial->setParity(QSerialPort::NoParity);
  serial->setStopBits(QSerialPort::OneStop);
  serial->setFlowControl(QSerialPort::NoFlowControl);

  if(!serial->open(QIODevice::ReadWrite)){

      qDebug("error when opening the serial port");
      QMessageBox::critical(this, tr("Error"), serial->errorString());
  }
}

void MainWindow::readData()
{
  data.append(serial->readAll());

  newDataRead = 1;

  if(!serial->putChar('B')){

      qDebug("error when sending over the serial port");
      QMessageBox::critical(this, tr("Error"), serial->errorString());
  }

  timer->start(); //act as a restart on the timer
}


void MainWindow::updateRaw(){

  ui->lcdNumber->display(data.size());

  fetch_button->setProgress(data.size()*100/33554432);    //33554432 = 32MB, the total size of the memory
}



void MainWindow::updateData(){

  if(newDataRead){

    ui->data->setText(QString(data.toHex()));
    update_table();
  }

  newDataRead = 0;
  fetch_button->hideProgressBar();
}



void MainWindow::fetch_memory()
{

  if(!serial->putChar('A')){

      qDebug("error when sending over the serial port");
      QMessageBox::critical(this, tr("Error"), serial->errorString());
  }

  ui->data->clear();
  this->data.clear();

  fetch_button->showProgressBar();
}



void MainWindow::update_table()
{
  char start_log_sequence[6] = {0xAA, 0x55, 0xFF, 0x00, 0x55, 0xAA};
  char start_values_sequence[3] = {0xF0, 0xF0, 0xA5};
  char stop_log_sequence[6] = {0xFF, 0x00, 0x55, 0xAA, 0x00, 0xFF};
  int start_of_log, start_of_values, end_of_log;
  int nbr_messages;
  QList<QByteArray> messages_names;



  //decode the name of the messages
  start_of_log = data.indexOf(QByteArray(start_log_sequence, 6));
  start_of_values = data.indexOf(QByteArray(start_values_sequence, 3));

  messages_names = data.mid(start_of_log+6, start_of_values-6).split(';');
  nbr_messages = messages_names.size();


  model->clear();
  model = new QStandardItemModel(0, nbr_messages, this);
  for(int i=0; i<nbr_messages; i++){

      model->setHorizontalHeaderItem(i, new QStandardItem(QString(messages_names.at(i))));
  }

  ui->table->setModel(model);



  //decode the values
  end_of_log = data.indexOf(QByteArray(stop_log_sequence, 6));
  if(end_of_log < 0) return;


  decodeProgressBar->show();
  decodeProgressBar->setValue(0);
  qApp->processEvents();    //we force qt to process the events to display the progress bar


  QByteArray values = data.mid(start_of_values+3, end_of_log-(start_of_values+3));

  for(int i=0; i< (values.size()/(nbr_messages*DATA_SIZE)); i++){ //for each pack of values (each row of the table)

    QByteArray subValues = values.mid(i*DATA_SIZE*nbr_messages, nbr_messages*DATA_SIZE);
    QList<QStandardItem *> items;

    for(int j=0; j<nbr_messages; j++){  //for each messages

        QByteArray currentValue = subValues.mid(j*DATA_SIZE, DATA_SIZE);
        double currentDoubleValue = 0;

        if(currentValue.size() < DATA_SIZE) break;

        for(int k=0; k<DATA_SIZE; k++){

          currentDoubleValue += currentValue.at(k)<<(8*k);
        }


        QStandardItem *item = new QStandardItem(QString::number(currentDoubleValue));
        items.append(item);
    }

    model->appendRow(items);

    int progress = (i*100)/(values.size()/(nbr_messages*DATA_SIZE));
    if(progress != decodeProgressBar->value()){

      ui->lcd_lines->display(model->rowCount());
      decodeProgressBar->setValue(i*100/(values.size()/(nbr_messages*DATA_SIZE)));
      qApp->processEvents();    //we force qt to process the events to display the progress bar
    }
  }

  ui->lcd_lines->display(model->rowCount());
  decodeProgressBar->hide();
}



void MainWindow::on_export_button_clicked()
{
  QString filename = QFileDialog::getSaveFileName(this, "Export data", "", "*.csv");
  if(filename.isEmpty()) return;
  if (!filename.endsWith(".csv", Qt::CaseInsensitive) ) filename += ".csv";

  QFile f(filename);
  if(!f.open(QIODevice::WriteOnly)){

      //error when opening the file
      return;
  }
  QTextStream file(&f);


  for(int i=0; i<model->columnCount(); i++){

    file << model->horizontalHeaderItem(i)->text().toLatin1() << ";";
  }

  file << "\n\r";

  for(int i=0; i<model->rowCount(); i++){

    for(int j=0; j<model->columnCount(); j++){

      file << model->item(i, j)->text().toLatin1() << ";";
    }

    file << "\n\r";
  }

  f.close();
}


void MainWindow::on_actionDump_memory_triggered()
{
  fetch_memory();
}


void MainWindow::on_actionClear_interface_triggered()
{
  data.clear();
  ui->lcdNumber->display(0);
  ui->data->setText("");
  ui->lcd_lines->display(0);
  model->clear();
}

void MainWindow::on_actionExport_data_triggered()
{
  on_export_button_clicked();
}



