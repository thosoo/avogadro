/****************************************************************************
** Meta object code from reading C++ file 'moleculefile.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../libavogadro/src/moleculefile.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'moleculefile.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.4.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
namespace {
struct qt_meta_stringdata_Avogadro__MoleculeFile_t {
    uint offsetsAndSizes[10];
    char stringdata0[23];
    char stringdata1[6];
    char stringdata2[1];
    char stringdata3[14];
    char stringdata4[15];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__MoleculeFile_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__MoleculeFile_t qt_meta_stringdata_Avogadro__MoleculeFile = {
    {
        QT_MOC_LITERAL(0, 22),  // "Avogadro::MoleculeFile"
        QT_MOC_LITERAL(23, 5),  // "ready"
        QT_MOC_LITERAL(29, 0),  // ""
        QT_MOC_LITERAL(30, 13),  // "firstMolReady"
        QT_MOC_LITERAL(44, 14)   // "threadFinished"
    },
    "Avogadro::MoleculeFile",
    "ready",
    "",
    "firstMolReady",
    "threadFinished"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__MoleculeFile[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   32,    2, 0x06,    1 /* Public */,
       3,    0,   33,    2, 0x06,    2 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       4,    0,   34,    2, 0x09,    3 /* Protected */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::MoleculeFile::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__MoleculeFile.offsetsAndSizes,
    qt_meta_data_Avogadro__MoleculeFile,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__MoleculeFile_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<MoleculeFile, std::true_type>,
        // method 'ready'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'firstMolReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'threadFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void Avogadro::MoleculeFile::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MoleculeFile *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->ready(); break;
        case 1: _t->firstMolReady(); break;
        case 2: _t->threadFinished(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (MoleculeFile::*)();
            if (_t _q_method = &MoleculeFile::ready; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (MoleculeFile::*)();
            if (_t _q_method = &MoleculeFile::firstMolReady; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
    }
    (void)_a;
}

const QMetaObject *Avogadro::MoleculeFile::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::MoleculeFile::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__MoleculeFile.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int Avogadro::MoleculeFile::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void Avogadro::MoleculeFile::ready()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void Avogadro::MoleculeFile::firstMolReady()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
