# BinDiffSynchronizer

Важно понимать разницу между персистным указателем на объект и указателем на персистный объект,


Создание и удаление:
    Создание и удаление персистных объектов и персистных указателей ничем не отличается от создания и удаление
    обычных объектов. Они создаются в одной и той же памяти, могут создаваться статически либо динамически.
    Другое дело создание и удаление объектов на которые указывают персистные указатели. Этим заведуют менеджеры
    адресных пространств соотвествующих типов объектов, т.к. для каждого типа объекта на который ссылается персистный
    указатель используется свой менеждер адресного пространства. Физически адресное пространство может быть диском,
    сетевым именем компьютера, объектной или обычной БД или ещё чем то ещё. Менеждер АП имеет специальные статические
    методы для выделения и освобождения памяти для объектов.
