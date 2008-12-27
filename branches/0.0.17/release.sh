export FTPII_VERSION=$1

cd `dirname $0`
wd=`pwd`
base=`basename $wd`
cd ..

cp -R $base $FTPII_VERSION && \
cd $FTPII_VERSION && \
make clean && \
rm -f *.dol *.elf && \
make && \
rm -rf build patches && \
mkdir ftpii && \
mv hbc/meta.xml ftpii && \
mv hbc/icon.png ftpii && \
cp ftpii.dol ftpii/boot.dol && \
rm -f *.elf *.dol && \
rm -rf hbc && \
(find . -name .svn | xargs rm -rf) && \
rm release.sh && \
cd .. && \
zip -r $FTPII_VERSION.zip $FTPII_VERSION && \
rm -rf $FTPII_VERSION