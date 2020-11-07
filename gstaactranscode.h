#ifndef __GST_AACTRANSCODE_H__
#define __GST_AACTRANSCODE_H__

G_BEGIN_DECLS

#define GST_TYPE_AACTRANSCODE \
  (gst_aactranscode_get_type())
#define GST_AACTRANSCODE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AACTRANSCODE,GstAacTranscode))
#define GST_AACTRANSCODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AACTRANSCODE,GstAacTranscodeClass))
#define GST_IS_AACTRANSCODE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AACTRANSCODE))
#define GST_IS_AACTRANSCODE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AACTRANSCODE))

typedef struct _GstAacTranscode GstAacTranscode;
typedef struct _GstAacTranscodeClass GstAacTranscodeClass;

struct _GstAacTranscode {
	GstBin bin;

	gint conv;
	gint multich_only;
	glong bitrate;

};

struct _GstAacTranscodeClass {
	GstBinClass parent_class;
};

G_END_DECLS

#endif /* __GST_AACTRANSCODE_H__ */
