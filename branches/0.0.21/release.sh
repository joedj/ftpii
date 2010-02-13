export VERSION=$1

cd `dirname $0`
wd=`pwd`
base=`basename $wd`
cd ..

cp -R $base $VERSION && \
cd $VERSION && \
make clean && \
make && \
mkdir ftpii && \
mv README.txt LICENSE.txt hbc/* ftpii && \
mv ftpii.elf ftpii/boot.elf && \
make clean && \
rm -rf Makefile data source hbc patches && \
(find . -name .svn | xargs rm -rf) && \
rm release.sh && \
zip -r ../$VERSION.zip ftpii && \
cd .. && \
rm -rf $VERSION
