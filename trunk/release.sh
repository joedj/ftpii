export FTPII_VERSION=$1

cd `dirname $0`
cd ..

cp -R ftpii $FTPII_VERSION && \
cd $FTPII_VERSION && \
make clean && \
rm -rf *.dol *.elf && \
make && \
rm -rf build && \
mkdir ftpii && \
mv hbc/meta.xml ftpii && \
mv hbc/icon.png ftpii && \
cp $FTPII_VERSION.elf ftpii/boot.elf && \
rm -f *.elf && \
rm -rf hbc && \
(find . -name .svn | xargs rm -rf) && \
rm release.sh && \
cd .. && \
zip -r $FTPII_VERSION.zip $FTPII_VERSION && \
rm -rf $FTPII_VERSION
