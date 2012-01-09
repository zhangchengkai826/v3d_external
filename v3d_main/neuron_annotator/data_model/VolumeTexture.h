#ifndef VOLUMETEXTURE_H
#define VOLUMETEXTURE_H

#include "NaSharedDataModel.h"
#include "DataColorModel.h"
#include "NaVolumeData.h"
#include "../DataFlowModel.h"
#include "Dimension.h"
#include <QObject>
#include "../../3drenderer/GLee_r.h"
#include <vector>

namespace jfrc {

class PrivateVolumeTexture; // forward declaration

/// Contains OpenGL texture data for 3D viewer representation of NeuronAnnotator volume
class VolumeTexture : public NaSharedDataModel<PrivateVolumeTexture>
{
    Q_OBJECT

public:
    VolumeTexture();
    bool initializeGL();
    void setDataFlowModel(const DataFlowModel& dataFlowModelParam);

signals:
    // When textures change, they must be uploaded in the main/OpenGL thread
    void neuronVisibilityTextureChanged();
    void colorMapTextureChanged();
    void volumeTexturesChanged();

public slots:
    bool updateVolume();
    void updateNeuronVisibilityTexture();

private:
    // semantic sugar
    typedef NaSharedDataModel<PrivateVolumeTexture> super;
    const NaVolumeData* volumeData;

public:
    /// Allows clients (such as Na3DViewer) to upload pixels in main/OpenGL thread.
    /// (because this operation MUST be done in the OpenGL thread)
    class Reader : public BaseReader
    {
    public:
        Reader(const VolumeTexture&);
        const jfrc::Dimension& originalImageSize() const;
        const jfrc::Dimension& usedTextureSize() const;
        const jfrc::Dimension& paddedTextureSize() const;
        // All of the following OpenGL-using methods must be call from the main/OpenGL thread only.
        bool uploadVolumeTexturesToVideoCardGL() const; // This one could take some time
        bool uploadNeuronVisibilityTextureToVideoCardGL() const;
        bool uploadColorMapTextureToVideoCardGL() const;
        const GLuint* Xtex_list() const;
        const GLuint* Ytex_list() const;
        const GLuint* Ztex_list() const;
    };

protected:
    class Writer : public BaseWriter
    {
    public:
        Writer(VolumeTexture&);
    };
};

} // namespace jfrc

#endif // VOLUMETEXTURE_H