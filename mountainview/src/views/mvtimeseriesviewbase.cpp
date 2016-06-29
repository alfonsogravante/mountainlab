/******************************************************
** See the accompanying README and LICENSE files
** Author(s): Jeremy Magland
** Created: 5/27/2016
*******************************************************/

#include "mvtimeseriesviewbase.h"
#include <math.h>

#include <QAction>
#include <QIcon>
#include <QImageWriter>
#include <QMouseEvent>
#include <QPainter>

struct TickStruct {
    TickStruct(QString str0, long min_pixel_spacing_between_ticks0, double tick_height0, double timepoint_interval0)
    {
        str = str0;
        min_pixel_spacing_between_ticks = min_pixel_spacing_between_ticks0;
        tick_height = tick_height0;
        timepoint_interval = timepoint_interval0;
        show_scale = false;
    }

    QString str;
    long min_pixel_spacing_between_ticks;
    double tick_height;
    double timepoint_interval;
    bool show_scale;
};

class mvtsvb_calculator {
public:
    //input
    DiskReadMda firings;
    MVEventFilter filter;
    QSet<int> labels_to_use;

    //output
    QVector<double> times;
    QVector<int> labels;

    void compute();
};

class MVTimeSeriesViewBasePrivate {
public:
    MVTimeSeriesViewBase* q;

    QVector<double> m_times;
    QVector<int> m_labels;

    QSet<int> m_labels_to_view;

    mvtsvb_calculator m_calculator;

    mvtsv_prefs m_prefs;

    MVRange m_selected_t_range;
    bool m_activated;

    long m_num_timepoints;

    QPointF m_left_click_anchor_pix;
    double m_left_click_anchor_time;
    MVRange m_left_click_anchor_t_range;
    bool m_left_click_dragging;

    void paint_cursor(QPainter* painter, double W, double H);
    void paint_markers(QPainter* painter, const QVector<double>& t0, const QVector<int>& labels, double W, double H);
    void paint_message_at_top(QPainter* painter, QString msg, double W, double H);
    void paint_time_axis(QPainter* painter, double W, double H);
    void paint_time_axis_unit(QPainter* painter, double W, double H, TickStruct TS);
    void paint_status_string(QPainter* painter, double W, double H, QString str);

    double time2xpix(double t);
    double xpix2time(double xpix);
    QRectF content_geometry();

    void zoom_out(double about_time, double frac = 0.8);
    void zoom_in(double about_time, double frac = 0.8);
    void scroll_to_current_timepoint();

    QString format_time(double tp);
    void update_cursor();
};

MVTimeSeriesViewBase::MVTimeSeriesViewBase(MVContext* context)
    : MVAbstractView(context)
{
    d = new MVTimeSeriesViewBasePrivate;
    d->q = this;
    d->m_selected_t_range = MVRange(-1, -1);
    d->m_activated = true;
    d->m_left_click_anchor_pix = QPointF(-1, -1);
    d->m_left_click_dragging = false;
    this->setMouseTracking(true);
    d->m_num_timepoints = 1;

    {
        QAction* a = new QAction(QIcon(":/images/zoom-in.png"), "Zoom In", this);
        a->setProperty("action_type", "toolbar");
        a->setToolTip("Zoom in. Alternatively, use the mouse wheel.");
        this->addAction(a);
        connect(a, SIGNAL(triggered(bool)), this, SLOT(slot_zoom_in()));
    }
    {
        QAction* a = new QAction(QIcon(":/images/zoom-out.png"), "Zoom Out", this);
        a->setProperty("action_type", "toolbar");
        a->setToolTip("Zoom out. Alternatively, use the mouse wheel.");
        this->addAction(a);
        connect(a, SIGNAL(triggered(bool)), this, SLOT(slot_zoom_out()));
    }

    QObject::connect(context, SIGNAL(currentTimepointChanged()), this, SLOT(update()));
    QObject::connect(context, SIGNAL(currentTimeRangeChanged()), this, SLOT(update()));
    QObject::connect(context, SIGNAL(currentTimepointChanged()), this, SLOT(slot_scroll_to_current_timepoint()));

    this->recalculateOn(context, SIGNAL(filteredFiringsChanged()));

    this->setFocusPolicy(Qt::StrongFocus);
}

MVTimeSeriesViewBase::~MVTimeSeriesViewBase()
{
    delete d;
}

void MVTimeSeriesViewBase::prepareCalculation()
{
    d->m_calculator.firings = mvContext()->firings();
    d->m_calculator.filter = mvContext()->eventFilter();
    d->m_calculator.labels_to_use = d->m_labels_to_view;
}

void MVTimeSeriesViewBase::runCalculation()
{
    d->m_calculator.compute();
}

void MVTimeSeriesViewBase::onCalculationFinished()
{
    d->m_times = d->m_calculator.times;
    d->m_labels = d->m_calculator.labels;
}

/*
void MVTimeSeriesViewBase::setTimesLabels(const QVector<double>& times, const QVector<int>& labels)
{
    d->m_times = times;
    d->m_labels = labels;
    update();
}
*/

void MVTimeSeriesViewBase::setNumTimepoints(long N)
{
    d->m_num_timepoints = N;
}

mvtsv_prefs MVTimeSeriesViewBase::prefs() const
{
    return d->m_prefs;
}

void MVTimeSeriesViewBase::setPrefs(mvtsv_prefs prefs)
{
    d->m_prefs = prefs;
}

void MVTimeSeriesViewBase::setLabelsToView(const QSet<int>& labels)
{
    d->m_labels_to_view = labels;
    this->recalculate();
}

QRectF MVTimeSeriesViewBase::contentGeometry()
{
    return d->content_geometry();
}

double MVTimeSeriesViewBase::time2xpix(double t) const
{
    return d->time2xpix(t);
}

double MVTimeSeriesViewBase::xpix2time(double x) const
{
    return xpix2time(x);
}

void MVTimeSeriesViewBase::resizeEvent(QResizeEvent* evt)
{
    QWidget::resizeEvent(evt);
}

void MVTimeSeriesViewBase::setTimeRange(MVRange range)
{
    mvContext()->setCurrentTimeRange(range);
}

MVRange MVTimeSeriesViewBase::timeRange()
{
    return mvContext()->currentTimeRange();
}

void MVTimeSeriesViewBase::setSelectedTimeRange(MVRange range)
{
    if (range == d->m_selected_t_range)
        return;
    d->m_selected_t_range = range;
    update();
}

void MVTimeSeriesViewBase::setActivated(bool val)
{
    if (d->m_activated == val)
        return;
    d->m_activated = val;
    update();
}

void MVTimeSeriesViewBase::setMarkersVisible(bool val)
{
    if (d->m_prefs.markers_visible == val)
        return;
    d->m_prefs.markers_visible = val;
    update();
}

void MVTimeSeriesViewBase::setMargins(double mleft, double mright, double mtop, double mbottom)
{
    d->m_prefs.mleft = mleft;
    d->m_prefs.mright = mright;
    d->m_prefs.mtop = mtop;
    d->m_prefs.mbottom = mbottom;
    update();
}

void MVTimeSeriesViewBase::paintEvent(QPaintEvent* evt)
{
    Q_UNUSED(evt)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    mvtsv_colors colors = d->m_prefs.colors;

    QColor back_col = colors.background_color;
    painter.fillRect(0, 0, width(), height(), back_col);

    double W0 = this->width();
    double H0 = this->height();

    double view_t1 = mvContext()->currentTimeRange().min;
    double view_t2 = mvContext()->currentTimeRange().max;

    // Event markers
    if (d->m_prefs.markers_visible) {
        QVector<double> times0;
        QVector<int> labels0;
        for (long i = 0; i < d->m_times.count(); i++) {
            double t0 = d->m_times[i];
            int l0 = d->m_labels[i];
            if ((view_t1 <= t0) && (t0 <= view_t2)) {
                times0 << t0;
                labels0 << l0;
            }
        }

        double min_avg_pixels_per_marker = 10; //probably add this to prefs
        if ((times0.count()) && (W0 / times0.count() >= min_avg_pixels_per_marker)) {
            d->paint_markers(&painter, times0, labels0, W0, H0);
        }
        else {
            if (times0.count()) {
                d->paint_message_at_top(&painter, "Zoom in to view markers", W0, H0);
            }
        }
    }

    // Cursor
    d->paint_cursor(&painter, W0, H0);

    // Time axis
    if (d->m_prefs.show_time_axis) {
        d->paint_time_axis(&painter, W0, H0);
    }

    // Status
    {
        QString str;
        if (d->m_prefs.show_current_timepoint) {
            double samplerate = mvContext()->sampleRate();
            if (samplerate) {
                str = QString("%1 (tp: %2)").arg(d->format_time(mvContext()->currentTimepoint())).arg((long)mvContext()->currentTimepoint());
            }
            else {
                str = QString("Sample rate is null (tp: %2)").arg((long)mvContext()->currentTimepoint());
            }
        }
        d->paint_status_string(&painter, W0, H0, str);
    }

    paintContent(&painter);
}

void MVTimeSeriesViewBase::mousePressEvent(QMouseEvent* evt)
{
    if (evt->button() == Qt::LeftButton) {
        d->m_left_click_anchor_pix = evt->pos();
        d->m_left_click_anchor_t_range = this->timeRange();
        d->m_left_click_anchor_time = d->xpix2time(evt->pos().x());
        d->m_left_click_dragging = false;
        emit clicked();
    }
    d->update_cursor();
}

void MVTimeSeriesViewBase::mouseReleaseEvent(QMouseEvent* evt)
{
    d->m_left_click_anchor_pix = QPointF(-1, -1);
    if (evt->button() == Qt::LeftButton) {
        if (!d->m_left_click_dragging) {
            double t0 = d->xpix2time(evt->pos().x());
            mvContext()->setCurrentTimepoint(t0);
        }
        d->m_left_click_dragging = false;
    }
    d->update_cursor();
}

void MVTimeSeriesViewBase::mouseMoveEvent(QMouseEvent* evt)
{
    if (!d->m_left_click_dragging) {
        if (d->m_left_click_anchor_pix.x() >= 0) {
            double dist = qAbs(d->m_left_click_anchor_pix.x() - evt->pos().x());
            if (dist >= 5) {
                d->m_left_click_dragging = true;
            }
        }
    }

    if (d->m_left_click_dragging) {
        // we want the evt->pos() to correspond to d->m_left_click_anchor_coord.t
        double t0 = d->xpix2time(evt->pos().x());
        double dt = t0 - d->m_left_click_anchor_time;
        this->setTimeRange(this->timeRange() + (-dt));
    }

    d->update_cursor();
}

void MVTimeSeriesViewBase::wheelEvent(QWheelEvent* evt)
{
    int delta = evt->delta();
    if (!(evt->modifiers() & Qt::ControlModifier)) {
        if (delta < 0) {
            d->zoom_out(mvContext()->currentTimepoint());
        }
        else if (delta > 0) {
            d->zoom_in(mvContext()->currentTimepoint());
        }
    }
    else {
        //This used to allow zooming at hover position -- probably not needed
        /*
        float frac = 1;
        if (delta < 0)
            frac = 1 / 0.8;
        else if (delta > 0)
            frac = 0.8;
        Vec2 coord = this->plot()->pixToCoord(vec2(evt->pos().x(), evt->pos().y()));
        d->do_zoom(coord.x, frac);
        */
    }
}

void MVTimeSeriesViewBase::keyPressEvent(QKeyEvent* evt)
{
    if (evt->key() == Qt::Key_Left) {
        MVRange trange = this->timeRange();
        double range = trange.max - trange.min;
        mvContext()->setCurrentTimepoint(mvContext()->currentTimepoint() - range / 10);
        d->scroll_to_current_timepoint();
    }
    else if (evt->key() == Qt::Key_Right) {
        MVRange trange = this->timeRange();
        double range = trange.max - trange.min;
        mvContext()->setCurrentTimepoint(mvContext()->currentTimepoint() + range / 10);
        d->scroll_to_current_timepoint();
    }
    else if (evt->key() == Qt::Key_Home) {
        mvContext()->setCurrentTimepoint(0);
        d->scroll_to_current_timepoint();
    }
    else if (evt->key() == Qt::Key_End) {
        mvContext()->setCurrentTimepoint(d->m_num_timepoints - 1);
        d->scroll_to_current_timepoint();
    }
    else if (evt->key() == Qt::Key_Equal) {
        d->zoom_in(mvContext()->currentTimepoint());
    }
    else if (evt->key() == Qt::Key_Minus) {
        d->zoom_out(mvContext()->currentTimepoint());
    }
    else {
        QWidget::keyPressEvent(evt);
    }
}

void MVTimeSeriesViewBase::slot_scroll_to_current_timepoint()
{
    d->scroll_to_current_timepoint();
}

void MVTimeSeriesViewBase::slot_zoom_in()
{
    d->zoom_in(mvContext()->currentTimepoint());
}

void MVTimeSeriesViewBase::slot_zoom_out()
{
    d->zoom_out(mvContext()->currentTimepoint());
}

void MVTimeSeriesViewBasePrivate::paint_cursor(QPainter* painter, double W, double H)
{
    Q_UNUSED(W)
    Q_UNUSED(H)

    double mtop = m_prefs.mtop;
    double mbottom = m_prefs.mbottom;

    if (m_selected_t_range.min < 0) {
        double x0 = time2xpix(q->mvContext()->currentTimepoint());
        QPointF p0(x0, mtop);
        QPointF p1(x0, H - mbottom);

        for (int pass = 1; pass <= 2; pass++) {
            QPainterPath path;
            QPointF pp = p0;
            int sign = -1;
            if (pass == 2) {
                pp = p1;
                sign = 1;
            }
            path.moveTo(pp.x(), pp.y() - 10 * sign);
            path.lineTo(pp.x() - 8, pp.y() - 2 * sign);
            path.lineTo(pp.x() + 8, pp.y() - 2 * sign);
            path.lineTo(pp.x(), pp.y() - 10 * sign);
            QColor col = QColor(50, 50, 220);
            painter->fillPath(path, QBrush(col));
        }

        QPainterPath path2;
        path2.moveTo(p0.x(), p0.y() + 10);
        path2.lineTo(p1.x(), p1.y() - 10);
        painter->setPen(QPen(QBrush(QColor(50, 50, 220, 60)), 0));
        painter->drawPath(path2);

        //painter->setPen(QPen(QBrush(QColor(50,50,220,180)),1));
        //painter->drawPath(path2);
    }

    if (m_selected_t_range.min >= 0) {
        double x0 = time2xpix(m_selected_t_range.min);
        QPointF p0(x0, mtop);
        QPointF p1(x0, H - mbottom);

        QPainterPath path;
        path.moveTo(p0.x(), p0.y());
        path.lineTo(p1.x(), p0.y());
        path.lineTo(p1.x(), p1.y());
        path.lineTo(p0.x(), p1.y());
        path.lineTo(p0.x(), p0.y());

        int pen_width = 6;
        QColor pen_color = QColor(150, 150, 150);
        painter->setPen(QPen(QBrush(pen_color), pen_width));
        painter->drawPath(path);
    }
}

struct MarkerRecord {
    double xpix;
    int label;
    int level;
};

struct MarkerRecord_comparer {
    bool operator()(const MarkerRecord& a, const MarkerRecord& b) const
    {
        if (a.xpix < b.xpix)
            return true;
        else if (a.xpix == b.xpix)
            return (a.level < b.level);
        else
            return false;
    }
};

void sort_by_xpix2(QList<MarkerRecord>& records)
{
    qSort(records.begin(), records.end(), MarkerRecord_comparer());
}

void MVTimeSeriesViewBasePrivate::paint_markers(QPainter* painter, const QVector<double>& times, const QVector<int>& labels, double W, double H)
{
    Q_UNUSED(W)
    double mtop = m_prefs.mtop;
    double mbottom = m_prefs.mbottom;

    mvtsv_colors colors = m_prefs.colors;

    QList<MarkerRecord> marker_recs;

    int min_dist = 20;

    for (long i = 0; i < times.count(); i++) {
        double t0 = times[i];
        int l0 = labels[i];
        double x0 = time2xpix(t0);
        MarkerRecord MR;
        MR.xpix = x0;
        MR.label = l0;
        MR.level = 0;
        marker_recs << MR;
    }
    sort_by_xpix2(marker_recs);
    for (long i = 1; i < marker_recs.count(); i++) {
        if (marker_recs[i - 1].xpix + min_dist >= marker_recs[i].xpix) {
            marker_recs[i].level = (marker_recs[i - 1].level + 1) % m_prefs.num_label_levels;
        }
    }
    QPen pen = painter->pen();
    pen.setColor(colors.marker_color);
    painter->setPen(pen);
    QFont font = painter->font();
    font.setPixelSize(m_prefs.label_font_height);
    painter->setFont(font);
    for (long i = 0; i < marker_recs.count(); i++) {
        MarkerRecord MR = marker_recs[i];
        QPointF p0(MR.xpix, mtop);
        QPointF p1(MR.xpix, H - mbottom);
        painter->drawLine(p0, p1);
        QRectF rect(MR.xpix - 30, mtop - 3 - m_prefs.label_font_height * (MR.level + 1), 60, m_prefs.label_font_height);
        painter->drawText(rect, Qt::AlignCenter | Qt::AlignVCenter, QString("%1").arg(MR.label));
    }
}

void MVTimeSeriesViewBasePrivate::paint_message_at_top(QPainter* painter, QString msg, double W, double H)
{
    Q_UNUSED(H)
    QPen pen = painter->pen();
    pen.setColor(m_prefs.colors.marker_color);
    painter->setPen(pen);
    QFont font = painter->font();
    font.setPixelSize(m_prefs.label_font_height);
    painter->setFont(font);

    QRectF rect(0, 0, W, m_prefs.mtop);
    painter->drawText(rect, Qt::AlignCenter | Qt::AlignVCenter, msg);
}

void MVTimeSeriesViewBasePrivate::paint_time_axis(QPainter* painter, double W, double H)
{
    double samplerate = q->mvContext()->sampleRate();
    long min_pixel_spacing_between_ticks = 30;

    double view_t1 = q->mvContext()->currentTimeRange().min;
    double view_t2 = q->mvContext()->currentTimeRange().max;

    QColor axis_color = m_prefs.colors.axis_color;

    QPen pen = painter->pen();
    pen.setColor(axis_color);
    painter->setPen(pen);

    QPointF pt1(m_prefs.mleft, H - m_prefs.mbottom);
    QPointF pt2(W - m_prefs.mright, H - m_prefs.mbottom);
    painter->drawLine(pt1, pt2);

    QList<TickStruct> structs;

    structs << TickStruct("1 ms", min_pixel_spacing_between_ticks, 2, 1e-3 * samplerate);
    structs << TickStruct("10 ms", min_pixel_spacing_between_ticks, 3, 10 * 1e-3 * samplerate);
    structs << TickStruct("100 ms", min_pixel_spacing_between_ticks, 4, 100 * 1e-3 * samplerate);
    structs << TickStruct("1 s", min_pixel_spacing_between_ticks, 5, 1 * samplerate);
    structs << TickStruct("10 s", min_pixel_spacing_between_ticks, 5, 10 * samplerate);
    structs << TickStruct("1 m", min_pixel_spacing_between_ticks, 5, 60 * samplerate);
    structs << TickStruct("10 m", min_pixel_spacing_between_ticks, 5, 10 * 60 * samplerate);
    structs << TickStruct("1 h", min_pixel_spacing_between_ticks, 5, 60 * 60 * samplerate);
    structs << TickStruct("1 day", min_pixel_spacing_between_ticks, 5, 24 * 60 * 60 * samplerate);

    for (int i = 0; i < structs.count(); i++) {
        double scale_pixel_width = W / (view_t2 - view_t1) * structs[i].timepoint_interval;
        if ((scale_pixel_width >= 60) && (!structs[i].str.isEmpty())) {
            structs[i].show_scale = true;
            break;
        }
    }

    for (int i = 0; i < structs.count(); i++) {
        paint_time_axis_unit(painter, W, H, structs[i]);
    }
}

/// TODO, change W,H to size throughout
void MVTimeSeriesViewBasePrivate::paint_time_axis_unit(QPainter* painter, double W, double H, TickStruct TS)
{
    Q_UNUSED(W)

    double view_t1 = q->mvContext()->currentTimeRange().min;
    double view_t2 = q->mvContext()->currentTimeRange().max;

    double pixel_interval = W / (view_t2 - view_t1) * TS.timepoint_interval;

    if (pixel_interval >= TS.min_pixel_spacing_between_ticks) {
        long i1 = (long)ceil(view_t1 / TS.timepoint_interval);
        long i2 = (long)floor(view_t2 / TS.timepoint_interval);
        for (long i = i1; i <= i2; i++) {
            double x0 = time2xpix(i * TS.timepoint_interval);
            QPointF p1(x0, H - m_prefs.mbottom);
            QPointF p2(x0, H - m_prefs.mbottom + TS.tick_height);
            painter->drawLine(p1, p2);
        }
    }
    if (TS.show_scale) {
        int label_height = 10;
        long j1 = view_t1 + 1;
        if (j1 < 1)
            j1 = 1;
        long j2 = j1 + TS.timepoint_interval;
        double x1 = time2xpix(j1);
        double x2 = time2xpix(j2);
        QPointF p1(x1, H - m_prefs.mbottom + TS.tick_height);
        QPointF p2(x2, H - m_prefs.mbottom + TS.tick_height);

        painter->drawLine(p1, p2);

        QRectF rect(p1.x(), p1.y() + 5, p2.x() - p1.x(), label_height);
        QFont font = painter->font();
        font.setPixelSize(label_height);
        painter->setFont(font);
        painter->drawText(rect, Qt::AlignCenter | Qt::AlignVCenter, TS.str);
    }
}

void MVTimeSeriesViewBasePrivate::paint_status_string(QPainter* painter, double W, double H, QString str)
{
    QColor text_col = m_prefs.colors.text_color;

    double status_height = 12;
    double voffset = 4;
    QFont font = painter->font();
    font.setPixelSize(status_height);
    painter->setFont(font);
    QPen pen = painter->pen();
    pen.setColor(text_col);
    painter->setPen(pen);

    QRectF rect(m_prefs.mleft, H - voffset - status_height, W - m_prefs.mleft - m_prefs.mright, status_height);
    painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, str);
}

double MVTimeSeriesViewBasePrivate::time2xpix(double t)
{

    double view_t1 = q->mvContext()->currentTimeRange().min;
    double view_t2 = q->mvContext()->currentTimeRange().max;

    if (view_t2 <= view_t1)
        return 0;

    double xpct = (t - view_t1) / (view_t2 - view_t1);
    double px = content_geometry().x() + xpct * content_geometry().width();
    return px;
}

double MVTimeSeriesViewBasePrivate::xpix2time(double xpix)
{
    double view_t1 = q->mvContext()->currentTimeRange().min;
    double view_t2 = q->mvContext()->currentTimeRange().max;

    double xpct = 0;
    if (content_geometry().width()) {
        xpct = (xpix - content_geometry().x()) / (content_geometry().width());
    }
    double t = view_t1 + xpct * (view_t2 - view_t1);
    return t;
}

QRectF MVTimeSeriesViewBasePrivate::content_geometry()
{
    double mleft = m_prefs.mleft;
    double mright = m_prefs.mright;
    double mtop = m_prefs.mtop;
    double mbottom = m_prefs.mbottom;
    return QRectF(mleft, mtop, q->width() - mleft - mright, q->height() - mtop - mbottom);
}

void MVTimeSeriesViewBasePrivate::zoom_out(double about_time, double frac)
{
    double about_xpix = time2xpix(about_time);
    q->setTimeRange(q->timeRange() * (1 / frac));
    double new_time = xpix2time(about_xpix);
    double dt = about_time - new_time;
    q->setTimeRange(q->timeRange() + (dt));
}

void MVTimeSeriesViewBasePrivate::zoom_in(double about_time, double frac)
{
    zoom_out(about_time, 1 / frac);
}

void MVTimeSeriesViewBasePrivate::scroll_to_current_timepoint()
{
    double t = q->mvContext()->currentTimepoint();
    MVRange trange = q->timeRange();
    if ((trange.min < t) && (t < trange.max))
        return;
    double range = trange.max - trange.min;
    if (t < trange.min) {
        q->setTimeRange(trange + (t - trange.min - range / 10));
    }
    else {
        q->setTimeRange(trange + (t - trange.max + range / 10));
    }
}

QString MVTimeSeriesViewBasePrivate::format_time(double tp)
{
    double samplerate = q->mvContext()->sampleRate();
    double sec = tp / samplerate;
    long day = (long)floor(sec / (24 * 60 * 60));
    sec -= day * 24 * 60 * 60;
    long hour = (long)floor(sec / (60 * 60));
    sec -= hour * 60 * 60;
    long minute = (long)floor(sec / (60));
    sec -= minute * 60;

    QString str;
    if (day)
        str += QString("%1 days ").arg(day);
    QString tmp_sec = QString("%1").arg(sec);
    if (sec < 10) {
        tmp_sec = QString("0%1").arg(sec);
    }
    str += QString("%1:%2:%3").arg(hour).arg(minute, 2, 10, QChar('0')).arg(tmp_sec);

    return str;
}

void MVTimeSeriesViewBasePrivate::update_cursor()
{
    if (m_left_click_dragging) {
        q->setCursor(Qt::OpenHandCursor);
    }
    else {
        q->setCursor(Qt::ArrowCursor);
    }
}

void mvtsvb_calculator::compute()
{
    times.clear();
    labels.clear();

    if (labels_to_use.isEmpty())
        return;

    firings = compute_filtered_firings_locally(firings, filter);

    long L = firings.N2();
    for (long i = 0; i < L; i++) {
        int label0 = firings.value(2, i);
        if (labels_to_use.contains(label0)) {
            times << firings.value(1, i);
            labels << label0;
        }
    }
}