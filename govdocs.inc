<?php

/**
 * @file 
 * Islandora govdocs 
 */

/**
 * Islandora Govdocs
 */
class IslandoraGovdocs {

  /**
   * Constructor
   * @param string $pid
   */
  function __construct($pid = '') {
    module_load_include('inc', 'fedora_repository', 'api/fedora_item');
    if (!empty($pid)) {
      $this->pid = $pid;
      $this->item = new Fedora_Item($pid);
    }
  }

  /**
   * Show field sets
   * @global string $base_url
   * @global user $user
   * @return string 
   */
  public function showFieldSets() {
    module_load_include('inc', 'islandora_govdocs', 'govdocs_pack_utils');
    module_load_include('inc', 'islandora_govdocs', 'add_pages');
    module_load_include('module', 'islandora_govdocs');
    module_load_include('inc', 'fedora_repository', 'CollectionClass');
    drupal_add_css(drupal_get_path('module', 'islandora_govdocs') . '/css/islandora_govdocs.css');
    global $base_url;
    $show_purge_tab = (!empty($_POST['form_id']) && ($_POST['form_id'] == 'islandora_govdocs_purge_pages_form'));
    $page_number = 1;
    $pages_selected = FALSE;
    $qstring = $_GET['q'];
    $qparts = explode('/', $qstring);
    $tail = end($qparts);
    if (is_numeric($tail)) {
      $pages_selected = TRUE;
      $page_number = $tail;
    }
    $class = new CollectionClass();
    $results = $class->getRelatedItems($this->pid);
    $page_items = $class->renderCollection($results, $this->pid, NULL, NULL, $page_number);
    $page_pids = govdocs_get_page_pids($this->pid);

    if (count($page_pids) > 0) {
      $hasPage = TRUE;
    }
    $tabset = array();

    global $user;
    $qs = '';
    if ($user->uid != 0) {
      $qs = '?uid=' . base64_encode($user->name . ':' . $user->pass);
    }

    $viewer_url = variable_get('fedora_base_url', '') . '/get/' . $this->pid . '/islandora:viewerSdef/getViewer' . $qs;
    $html = '<iframe src="' . $viewer_url . '" scrolling="0" frameborder="0" style="width: 100%; height: 800px;">Errors: unable to load viewer</iframe>';
    $tabset['pages_tab'] = array(
      // #type and #title are the minimum requirements.
      '#type' => 'tabpage',
      '#title' => t('Pages'),
      '#selected' => $pages_selected,
      '#content' => $page_items,
    );

    $tabset['first_tab'] = array(
      // #type and #title are the minimum requirements.
      '#type' => 'tabpage',
      '#title' => t('Description'),
      // This will be the content of the tab.
      '#content' => islandora_book_create_book_view($this->pid),
    );

    if ($hasPage) {
      $tabset['second_tab'] = array(
        '#type' => 'tabpage',
        '#title' => t('Read'),
        '#content' => $html
      );
    }

    if (!$hasPage && user_access('ingest new fedora objects')) {
      $tabset['third_tab'] = array(
        '#type' => 'tabpage',
        '#title' => t('Add pages'),
        '#content' => drupal_get_form('book_add_pages_form', $this->pid),
      );
    }
    
    if (!$hasPage && user_access('ingest new fedora objects')) {
      $tabset['fourth_tab'] = array(
        '#type' => 'tabpage',
        '#title' => t('Add subsection'),
        '#content' => drupal_get_form('govdocs_management_form', $this->pid, $content_models),
      );
    }
    
    if ($hasPage && user_access('purge objects and datastreams')) {
      $tabset['fifth_tab'] = array(
        '#type' => 'tabpage',
        '#title' => t('Purge all pages'),
        '#selected' => $show_purge_tab,
        '#content' => drupal_get_form('islandora_book_purge_pages_form', $this->pid),
      );
    }
    return $tabset;
  }

}


class GovdocsCollectionClass extends CollectionClass {

  public static $COLLECTION_CLASS_COLLECTION_POLICY_STREAM = 'COLLECTION_POLICY';
  public static $COLLECTION_CLASS_COLLECTION_VIEW_STREAM = 'COLLECTION_VIEW';
  private $contentModelStream = NULL;
  private $collectionPolicyStream = NULL;
  private $collectionViewStream = NULL;
  public $collectionObject = NULL;

  /**
   * Creates a collection object. Optionally can associate it with a single collection with parameter $pid.
   *
   * @param string $pid The pid of the collection to represent.
   * @return CollectionClass
   */
  function __construct($pid = NULL) {
    if (!empty($pid)) {
      module_load_include('inc', 'fedora_repository', 'ObjectHelper');
      $this->collectionObject = new ObjectHelper($pid);
      $this->pid = $pid;
    }
  }
  
  /**
   * show field sets ?
   * @global type $base_url
   * @global type $user
   * @param type $page_number
   * @return string 
   */
  function showGovdocsFieldSets($page_number) {
    module_load_include('inc', 'fedora_repository', 'api/fedora_item');
    module_load_include('inc', 'fedora_repository', 'CollectionManagement');
    module_load_include('inc', 'fedora_repository', 'BatchIngest');
    module_load_include('inc', 'islandora_govdocs', 'add_pages');
    global $base_url;
    $tabset = array();
    global $user;
    $objectHelper = new ObjectHelper();
    $item = new Fedora_Item($this->pid);
    $query = NULL;
    if ($item->exists() && array_key_exists('QUERY', $item->datastreams)) {
      $query = $item->get_datastream_dissemination('QUERY');
    }
    $results = $this->getRelatedItems($this->pid, $query);

    $collection_items = $this->renderCollection($results, $this->pid, NULL, NULL, $page_number);
    $collection_item = new Fedora_Item($this->pid);
    // Check the form post to see if we are in the middle of an ingest operation.
    $show_ingest_tab = (!empty($_POST['form_id']) && $_POST['form_id'] == 'fedora_repository_ingest_form');
    $add_to_collection = $this->getIngestInterface();

    $show_batch_tab = TRUE;
    $policy = CollectionPolicy::loadFromCollection($this->pid, TRUE);
    $content_models = $policy->getContentModels();
    if (count($content_models) == 1 && $content_models[0]->pid == "islandora:collectionCModel") {
      $show_batch_tab = FALSE;
    }
    if (!$show_ingest_tab) {
      $view_selected = TRUE;
    }
    if(!$collection_items){
      $view_selected = FALSE;
      $add_selected = TRUE;
    }
    $view_selected = !$show_ingest_tab;


    $tabset['view_tab'] = array(
      '#type' => 'tabpage',
      '#title' => 'View',
      '#selected' => $view_selected,
      '#content' => $collection_items,
     
    );
    $tabset['add_tab'] = array(
      '#type' => 'tabpage',
      '#title' => t('Add'),
      '#selected' => $add_selected,
      // This will be the content of the tab.
      '#content' => $add_to_collection,
    );
    if (user_access('manage collections')) {
      $tabset['add_collection_tab'] = array(
        // #type and #title are the minimum requirements.
        '#type' => 'tabpage',
        '#title' => t('Manage This Collection'),
        // This will be the content of the tab.
        '#content' => drupal_get_form('govdocs_management_form', $this->pid, $content_models),
        '#selected' => FALSE,
      );
    }
    if ($show_batch_tab && user_access('create batch process')) {
      $tabset['batch_ingest_tab'] = array(
        // #type and #title are the minimum requirements.
        '#type' => 'tabpage',
        '#title' => t('Batch Ingest'),
        // This will be the content of the tab.
        '#content' => drupal_get_form('batch_creation_form', $this->pid, $content_models),
      );
    }
    return $tabset;
  } 
  
}

