#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>

#ifndef METADATA_VERSION
#define METADATA_VERSION "0.1.0"
#endif

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("metadata"));
    QApplication::setApplicationDisplayName(QStringLiteral("metadata"));
    QApplication::setApplicationVersion(QString::fromLatin1(METADATA_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Yousefvand"));
    QApplication::setOrganizationDomain(QStringLiteral("github.com/yousefvand"));
    QApplication::setDesktopFileName(QStringLiteral("io.github.yousefvand.metadata"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("View, add, edit, and remove file metadata."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QStringLiteral("File to open."),
                                 QStringLiteral("[file]"));
    parser.process(application);

    MainWindow window;
    window.show();

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        window.openPath(positional.first());
    }

    return QApplication::exec();
}
