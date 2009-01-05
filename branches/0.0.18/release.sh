export FTPII_VERSION=$1

cd `dirname $0`
wd=`pwd`
base=`basename $wd`
cd ..

cp -R $base $FTPII_VERSION && \
cd $FTPII_VERSION/source && \
make clean && \
make && \
cd .. && \
mkdir ftpii && \
mv README.txt LICENSE.txt hbc/* ftpii && \
cp source/ftpii.dol ftpii/boot.dol && \
rm -rf source hbc patches && \
(find . -name .svn | xargs rm -rf) && \
rm release.sh && \
cd .. && \
zip -r $FTPII_VERSION.zip $FTPII_VERSION && \
rm -rf $FTPII_VERSION
