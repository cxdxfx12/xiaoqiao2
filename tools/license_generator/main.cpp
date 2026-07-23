#include <QApplication>
#include <QIcon>
#include "license_generator_widget.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    app.setApplicationName("授权生成器");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("杭州喵喵至家网络有限公司");
    app.setOrganizationDomain("hbdxm.com");

    freight::tools::LicenseGeneratorWidget w;
    w.setWindowTitle("小乔运费结算 - 授权生成器");
    w.resize(520, 560);
    w.show();

    return app.exec();
}
