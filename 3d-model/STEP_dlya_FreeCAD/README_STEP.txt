STEP-модели лифта (настоящие твёрдые тела, не сетки)

Как открыть в FreeCAD:  Файл -> Открыть -> выбрать .step файл.
  2_section_etazh.step - секция-этаж (главная деталь для правки)
  assembly_3floors.step - вся шахта в сборе (3 этажа), с цветами
Остальные файлы - детали в позиции для печати, как в STL.

Как править в FreeCAD (верстак Part / Part Design):
  - отверстие под кнопку/LED: круглые грани видны как цилиндры; проще вырезать новое
    (Part -> Primitives -> Cylinder + Part -> Boolean -> Cut) или править params.py (см. ниже)
  - добавить/убрать материал: Part -> Primitives (Box, Cylinder) + Boolean Union/Cut

Параметрический способ (рекомендуется): все размеры в params.py.
  pip install cadquery
  python3 cq_model.py 3     # число этажей; пересоздаёт out/step/*.step
