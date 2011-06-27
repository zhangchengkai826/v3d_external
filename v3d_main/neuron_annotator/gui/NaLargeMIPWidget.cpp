#include "NaLargeMIPWidget.h"
#include <cassert>
#include <vector>
#include <map>

/////////////////////////////
// MipDisplayImage methods //
/////////////////////////////

MipDisplayImage::MipDisplayImage() : originalData(this)
{
    connect(&originalData, SIGNAL(processedXColumn(int)),
            this, SLOT(processedXColumnSlot(int)));
    connect(&originalData, SIGNAL(intensitiesUpdated()),
            this, SLOT(onDataIntensitiesUpdated()));
}

MipDisplayImage::~MipDisplayImage()
{
    neuronHighlightImages_t::iterator i_n;
    for (i_n = neuronHighlightImages.begin(); i_n != neuronHighlightImages.end(); ++i_n) {
        delete *i_n;
        *i_n = NULL;
    }
}

void MipDisplayImage::processedXColumnSlot(int c) {
    // qDebug() << "foo " << c;
    emit processedXColumn(c);
}

void MipDisplayImage::loadImageData(const My4DImage* img, const My4DImage* maskImg) {
    load4DImage(img, maskImg);
}

void MipDisplayImage::load4DImage(const My4DImage* img, const My4DImage* maskImg)
{
    QSize imageSize(img->getXDim(), img->getYDim());
    image = QImage(imageSize, QImage::Format_RGB32);
    originalData.loadMy4DImage(img, maskImg);
    brightnessCalibrator.setHdrRange(originalData.dataMin, originalData.dataMax);
    // brightnessCalibrator.setGamma(1.0);
    updateCorrectedIntensities();
    emit initialImageDataLoaded();

    // Populate neuron highlight images
    if (maskImg)
    {
        // First delete any stale highlight images
        neuronHighlightImages_t::iterator i_n;
        for (i_n = neuronHighlightImages.begin(); i_n != neuronHighlightImages.end(); ++i_n) {
            delete *i_n;
            *i_n = NULL;
        }
        // then create a new batch of highlight images
        neuronHighlightImages.resize(originalData.numNeurons, NULL);
        for (i_n = neuronHighlightImages.begin(); i_n != neuronHighlightImages.end(); ++i_n) {
            *i_n = new QImage(imageSize, QImage::Format_ARGB32);
            QImage& img = **i_n;
            img.fill(Qt::transparent);
        }
        // Fill in the masks
        QColor color;
        for (int y = 0; y < imageSize.height(); ++y) {
            for (int x = 0; x < imageSize.width(); ++x) {
                for (int z = 0; z < maskImg->getZDim(); ++z) {
                    int neuronIndex = maskImg->at(x, y, z);
                    if (neuronIndex < 0) continue;
                    assert(neuronIndex >= 0);
                    assert(neuronIndex < neuronHighlightImages.size());
                    QImage& img = *neuronHighlightImages[neuronIndex];
                    assert(img.width() == imageSize.width());
                    assert(img.height() == imageSize.height());
                    QRgb * scanline = (QRgb*) img.scanLine(y);
                    QRgb& pixel = scanline[x];
                    color = Qt::yellow; // TODO use neuron color
                    color.setAlpha(120);
                    pixel = color.rgba();
                }
            }
        }
    }
}

void MipDisplayImage::onDataIntensitiesUpdated()
{
    // Default display exactly entire intensity range
    // displayMin = 0.0; // example ct image has a very large minimum
    // brightnessCalibrator.setHdrRange(originalData.dataMin, originalData.dataMax);
    // brightnessCalibrator.setGamma(1.0);
    updateCorrectedIntensities();
    emit initialImageDataLoaded();
}

void MipDisplayImage::updateCorrectedIntensities()
{
    for (int y = 0; y < originalData.nRows(); ++y) {
        QRgb* scanLine = (QRgb*) image.scanLine(y); // faster than setPixel() for 32 bit rgb...
        for (int x = 0; x < originalData.nColumns(); ++x) {
            unsigned char red, green, blue;
            if (originalData.nChannels() == 1)
                red = green = blue = getCorrectedIntensity(x, y, 0);
            else {
                red = getCorrectedIntensity(x, y, 0);
                green = getCorrectedIntensity(x, y, 1);
                blue = getCorrectedIntensity(x, y, 2);
            }
            scanLine[x] = qRgb(red, green, blue);
        }
    }
}

void MipDisplayImage::setGamma(float gamma)
{
    if (gamma == brightnessCalibrator.getGamma()) return;
    brightnessCalibrator.setGamma(gamma);
    updateCorrectedIntensities();
}

// i_in as original float value
// Output in range 0-255
unsigned char MipDisplayImage::getCorrectedIntensity(float i_in) const
{
    unsigned char answer =  (unsigned char)
                            ((brightnessCalibrator.getCorrectedIntensity(i_in) * 255.0f) + 0.4999);
    return answer;
}

unsigned char MipDisplayImage::getCorrectedIntensity(int x, int y, int c) const
{
    return getCorrectedIntensity(originalData[x][y][c]);
}

void MipDisplayImage::toggleNeuronDisplay(int neuronIx, bool checked)
{
    // qDebug() << "MipDisplayImage toggleNeuronDisplay";
    originalData.toggleNeuronDisplay(neuronIx, checked);
}

//////////////////////////////
// NaLargeMIPWidget methods //
//////////////////////////////

NaLargeMIPWidget::NaLargeMIPWidget(QWidget * parent)
    : Na2DViewer(parent)
    , mipImage(NULL)
    , imageUpdateThread(this)
    , highlightedNeuronMaskPixmap(200, 200)
    , highlightedNeuronIndex(-1)
{
    // Test image
    pixmap = QPixmap(200, 200);
    pixmap.fill(Qt::black);
    highlightedNeuronMaskPixmap.fill(Qt::transparent);
    updateDefaultScale();
    resetView();

    setCursor(Qt::OpenHandCursor);
    imageUpdateThread.start();

    // Progress bar for when image is being processed
    progressBar = new QProgressBar(this);
    progressBar->setValue(24);
    QGridLayout * gridLayout = new QGridLayout(this);
    gridLayout->addWidget(progressBar, 0, 0, 1, 1);
    progressBar->hide();
    connect(progressBar, SIGNAL(valueChanged(int)),
            this, SLOT(update()));
    connect(&mouseClickManager, SIGNAL(singleClick(QPoint)),
            this, SLOT(onMouseSingleClick(QPoint)));
    connect(this, SIGNAL(hoverNeuronChanged(int)),
            this, SLOT(onHighlightedNeuronChanged(int)));
    connect(this, SIGNAL(mouseLeftDragEvent(int, int, QPoint)),
            this, SLOT(translateImage(int,int)));
}

NaLargeMIPWidget::~NaLargeMIPWidget()
{
    if (mipImage) {
        delete mipImage;
        mipImage = NULL;
    }
}

bool NaLargeMIPWidget::loadMy4DImage(const My4DImage* img, const My4DImage* maskImg)
{
    int imageC = img->getCDim();
    // Unsure how to map colors if not 1 or 3 colors
    if ((imageC != 1)  && (imageC != 3)) {
        qDebug() << "Error: number of channels = " << img->getCDim();
        return false;
    }
    // Delegate computation of maximum intensity projection
    // to MipDisplayImage class
    if (mipImage) {
        delete mipImage;
        mipImage = NULL;
    }
    // Put loading of image data into another thread
    qDebug() << "Starting MIP data load";
    mipImage = new MipDisplayImage();
    mipImage->moveToThread(&imageUpdateThread);
    // qDebug() << "GUI thread = " << QThread::currentThread();
    // qDebug() << "MIP thread = " << &imageUpdateThread;
    connect(this, SIGNAL(volumeDataUpdated(const My4DImage*, const My4DImage*)),
            mipImage, SLOT(loadImageData(const My4DImage*, const My4DImage*)));
    connect(mipImage, SIGNAL(initialImageDataLoaded()),
            this, SLOT(initializePixmap()));
    connect(this, SIGNAL(neuronDisplayToggled(int, bool)),
            mipImage, SLOT(toggleNeuronDisplay(int,bool)));
    progressBar->setMaximum(img->getXDim());
    connect(mipImage, SIGNAL(processedXColumn(int)),
            progressBar, SLOT(setValue(int)));
    progressBar->show();
    emit volumeDataUpdated(img, maskImg); // Start image processing in another thread.
    return true;
}

void NaLargeMIPWidget::updatePixmap()
{
    pixmap = QPixmap::fromImage(mipImage->image);
    updateDefaultScale();
}

void NaLargeMIPWidget::initializePixmap()
{
    progressBar->hide();
    updatePixmap();
    // resetView();
    update();
    // qDebug() << "Finished MIP data load";
}

// Want to distinguish between double click and single click events
void NaLargeMIPWidget::onMouseSingleClick(QPoint pos)
{
    int neuronIx = neuronAt(pos);
    if (neuronIx > 0) {
        emit neuronClicked(neuronIx);
        // qDebug() << "clicked Neuron " << neuronAt(pos);
    }
}
void NaLargeMIPWidget::setGammaBrightness(qreal gamma)
{
    if (! mipImage) return;
    mipImage->setGamma((float)gamma);
    // qDebug() << "set gamma";
    updatePixmap();
    update();
}

void NaLargeMIPWidget::resetView()
{
    cameraModel.setScale(1.0); // fit to window
    qreal focusZ = cameraModel.focus().z();
    Vector3D newFocus(pixmap.size().width()/2.0,
                pixmap.size().height()/2.0,
                focusZ);
    // cerr << newFocus << __LINE__ << __FILE__;
    cameraModel.setFocus(newFocus);
}

void NaLargeMIPWidget::resizeEvent(QResizeEvent * event) {
    updateDefaultScale();
}

void NaLargeMIPWidget::paintIntensityNumerals(QPainter& painter)
{
    if (!mipImage) return;
    // qDebug() << "numerals";
    QPointF v_img_upleft = X_img_view * painter.viewport().topLeft();
    QPointF v_img_downright = X_img_view * painter.viewport().bottomRight();
    // qDebug() << v_img_upleft;
    // qDebug() << v_img_downright;

    // clear transform for text rendering, otherwise font size is harder to manage
    painter.resetTransform();

    QFont font = painter.font();
    float scale = defaultScale * cameraModel.scale();
    font.setPixelSize(scale/4.0);
    font.setStyleStrategy(QFont::NoAntialias); // text rendering can be slow
    painter.setFont(font);

    // qDebug() << "nColumns = " << mipImage->originalData.nColumns();
    // qDebug() << "nRows = " << mipImage->originalData.nRows();

    // Iterate over only the image pixels that are visible
    for (int x = int(v_img_upleft.x() - 0.5); x <= int(v_img_downright.x() + 0.5); ++x) {
        // qDebug() << "x = " << x;
        if (x < 0)
            continue;
        if (x >= mipImage->originalData.nColumns())
            continue;
        for (int y = int(v_img_upleft.y() - 0.5); y <= int(v_img_downright.y() + 0.5); ++y) {
            // qDebug() << "y = " << y;
            if (y < 0)
                continue;
            if (y >= mipImage->originalData.nRows())
                continue;
            // Transform image pixel coordinates back to viewport coordinates
            QPointF v = X_view_img * QPointF(x, y);
            // qDebug() << x << ", " << y << "; " << v.x() << ", " << v.y();
            // Print original data intensity, not displayed intensity
            // But choose font color based on displayed intensity.
            unsigned int red = qRed(mipImage->image.pixel(x, y));
            unsigned int green = qGreen(mipImage->image.pixel(x, y));
            unsigned int blue = qBlue(mipImage->image.pixel(x, y));
            // human color perception is important here
            float displayIntensity = 0.30 * red + 0.58 * green + 0.12 * blue;
            if (displayIntensity < 128)
                painter.setPen(Qt::white);
            else
                painter.setPen(Qt::black);

            int nC = mipImage->originalData.nChannels();
            float lineHeight = scale / (nC + 1.0);
            // Write a neat little column of numbers inside each pixel
            for (int c = 0; c < nC; ++c) {
                float val = mipImage->originalData[x][y][c];
                painter.drawText(QRectF(v.x(), v.y() + (c + 0.5) * lineHeight, scale, lineHeight),
                                 Qt::AlignHCenter | Qt::AlignVCenter,
                                 QString("%1").arg(val));
            }
        }
    }
}

void NaLargeMIPWidget::paintEvent(QPaintEvent *event)
{
    // qDebug() << "paint MIP " << width() << ", " << height();
    painter.begin(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // first fill background with black
    // painter.fillRect(0, 0, width(), height(), Qt::black);

    // adjust painter coordinate system to place image correctly
    transformPainterToCurrentCamera(painter);

    painter.drawPixmap(0, 0, pixmap); // magic!
    if (highlightedNeuronIndex > 0) { // zero means background
        if (highlightedNeuronMaskPixmap.size() != pixmap.size()) {
            // qDebug() << "pixmap size = " << pixmap.size();
            // qDebug() << "but highlight size = " << highlightedNeuronMaskPixmap.size();
        }
        else {
            // qDebug() << "Painting highlight for neuron " << highlightedNeuronIndex;
            // painter.drawPixmap(0, 0, highlightedNeuronMaskPixmap); // disable for now...
        }
    }

    if (bPaintCrosshair) paintCrosshair(painter);

    // At large zoom levels, write the intensity values at each pixel
    float scale = defaultScale * cameraModel.scale();
    if (scale > 40) { // 40 display pixels per image pixel
        paintIntensityNumerals(painter);
    }
    painter.end();
}

// Zoom using mouse wheel
void NaLargeMIPWidget::wheelEvent(QWheelEvent * e) // mouse wheel
{
    double oldZoom = cameraModel.scale();
    wheelZoom(e->delta());
    double factor = cameraModel.scale()/oldZoom;

    // Zoom like in Google earth depends on cursor position
    bool doSmartZoom = false;
    // Smart zoom only when zooming *in*, not when zooming *out*,
    // to minimize loss of painstakingly adjusted foucs.
    // if (e->delta() < 0) doSmartZoom = true; // adjust focus when zooming in
    if (doSmartZoom) {
        double scale = cameraModel.scale() * defaultScale;
        double dx = e->pos().x() - width()/2.0;
        double dy = e->pos().y() - height()/2.0;
        double dx2 = ((factor - 1.0) * dx) / scale;
        double dy2 = ((factor - 1.0) * dy) / scale;
        Vector3D newFocus = cameraModel.focus() + Vector3D(flip_X * dx2, flip_Y * dy2, 0);
        // cerr << newFocus << __LINE__ << __FILE__;
        cameraModel.setFocus(newFocus);
    }
    update();
}

/* virtual */
void NaLargeMIPWidget::mousePressEvent(QMouseEvent * event)
{
    super::mousePressEvent(event);
    // Feedback for starting a translation drag operation
    if (event->buttons() & Qt::LeftButton)
        setCursor(Qt::ClosedHandCursor);
}

/* virtual */
void NaLargeMIPWidget::mouseReleaseEvent(QMouseEvent * event)
{
    super::mouseReleaseEvent(event);
    setCursor(Qt::OpenHandCursor);
}

int NaLargeMIPWidget::neuronAt(const QPoint& p)
{
    int answer = -1;
    if (!mipImage) return answer;
    QPointF v_img = X_img_view * QPointF(p);
    int x = v_img.x();
    int y = v_img.y();
    if (x < 0) return answer;
    if (y < 0) return answer;
    if (x >= mipImage->originalData.nColumns()) return answer;
    if (y >= mipImage->originalData.nRows()) return answer;
    answer = mipImage->originalData[x][y].neuronIndex;
    return answer;
}

void NaLargeMIPWidget::onHighlightedNeuronChanged(int neuronIx)
{
    if (neuronIx <= 0) return;
    if (! mipImage) return;
    if (mipImage->neuronHighlightImages.size() <= neuronIx) return;
    QImage * highlightImage = mipImage->neuronHighlightImages[neuronIx];
    if (! highlightImage) return;
    // qDebug() << "Switching to neuron " << neuronIx;
    highlightedNeuronMaskPixmap = QPixmap::fromImage(*highlightImage);
    update();
}

// Drag in widget to translate the MIP image in x,y
void NaLargeMIPWidget::mouseMoveEvent(QMouseEvent * event)
{
    super::mouseMoveEvent(event);

    // Hover action: status message
    // Write status message when hovering with mouse.
    // Notice statement "setMouseTracking(true)" in constructor.
    if (Qt::NoButton == event->buttons())
    {
        // Hover to show (x, y, value) in status bar
        if (!mipImage) return;
        QPointF v_img = X_img_view * QPointF(event->pos());
        int x = v_img.x();
        int y = v_img.y();
        int z = 0;
        int neuronIx = -1;
        QString value("<None>"); // default value
        if ( (x >= 0) && (x < mipImage->originalData.nColumns())
            && (y >= 0) && (y < mipImage->originalData.nRows()) )
        {
            const MipData::Pixel& pixel = mipImage->originalData[x][y];
            z = pixel.z;
            value = "";
            int nC = mipImage->originalData.nChannels();
            if (nC > 1) value += "[";
            for (int c = 0; c < nC; ++c) {
                if (c > 0) value += ", ";
                float val = pixel[c];
                value += QString("%1").arg(val, 4);
            }
            if (nC > 1) value += "]";
            neuronIx = pixel.neuronIndex;
        }

        QString msg;
        msg = QString("x =%1, y =%2, z =%3, value =%4")
                      .arg(x, 3)
                      .arg(y, 3)
                      .arg(z, 3)
                      .arg(value);


        if (neuronIx > 0) { // Zero means background
            msg = QString("Neuron %1; ").arg(neuronIx, 2) + msg;
            if (neuronIx != highlightedNeuronIndex) {
                highlightedNeuronIndex = neuronIx;
                emit(hoverNeuronChanged(neuronIx));
            }
        }

        emit statusMessage(msg);
        return;
    }
}

void NaLargeMIPWidget::annotationModelUpdate(QString updateType)
{
    if (! mipImage) return;
    if (updateType.startsWith("NEURONMASK")) {
        QList<QString> list=updateType.split(QRegExp("\\s+"));
        QString indexString=list.at(1);
        QString checkedString=list.at(2);
        int index=indexString.toInt();
        bool checked=(checkedString.toInt()==1);
        // Use signal, so image update can occur in non-gui thread
        // qDebug() << "neuronDisplayToggled";
        emit neuronDisplayToggled(index, checked);
    }
}



